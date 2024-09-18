import os
import pytest
import redis
import asyncio
from redis import asyncio as aioredis

from . import dfly_multi_test_args, dfly_args
from .instance import DflyStartException
from .utility import batch_fill_data, gen_test_data, EnvironCntx


@dfly_multi_test_args({"keys_output_limit": 512}, {"keys_output_limit": 1024})
class TestKeys:
    async def test_max_keys(self, async_client: aioredis.Redis, df_server):
        max_keys = df_server["keys_output_limit"]
        pipe = async_client.pipeline()
        batch_fill_data(pipe, gen_test_data(max_keys * 3))
        await pipe.execute()
        keys = await async_client.keys()
        assert len(keys) in range(max_keys, max_keys + 512)


@pytest.fixture(scope="function")
def export_dfly_password() -> str:
    pwd = "flypwd"
    with EnvironCntx(DFLY_requirepass=pwd):
        yield pwd


async def test_password(df_factory, export_dfly_password):
    with df_factory.create() as dfly:
        # Expect password form environment variable
        with pytest.raises(redis.exceptions.AuthenticationError):
            async with aioredis.Redis(port=dfly.port) as client:
                await client.ping()
        async with aioredis.Redis(password=export_dfly_password, port=dfly.port) as client:
            await client.ping()

    # --requirepass should take precedence over environment variable
    requirepass = "requirepass"
    with df_factory.create(requirepass=requirepass) as dfly:
        # Expect password form flag
        with pytest.raises(redis.exceptions.AuthenticationError):
            async with aioredis.Redis(port=dfly.port, password=export_dfly_password) as client:
                await client.ping()
        async with aioredis.Redis(password=requirepass, port=dfly.port) as client:
            await client.ping()


"""
Make sure that multi-hop transactions can't run OOO.
"""

MULTI_HOPS = """
for i = 0, ARGV[1] do
  redis.call('INCR', KEYS[1])
end
"""


@dfly_args({"proactor_threads": 1})
async def test_txq_ooo(async_client: aioredis.Redis, df_server):
    async def task1(k, h):
        c = aioredis.Redis(port=df_server.port)
        for _ in range(100):
            await c.eval(MULTI_HOPS, 1, k, h)

    async def task2(k, n):
        c = aioredis.Redis(port=df_server.port)
        for _ in range(100):
            pipe = c.pipeline(transaction=False)
            pipe.lpush(k, 1)
            for _ in range(n):
                pipe.blpop(k, 0.001)
            await pipe.execute()

    await asyncio.gather(
        task1("i1", 2), task1("i2", 3), task2("l1", 2), task2("l1", 2), task2("l1", 5)
    )


async def test_arg_from_environ_overwritten_by_cli(df_factory):
    with EnvironCntx(DFLY_port="6378"):
        with df_factory.create(port=6377):
            client = aioredis.Redis(port=6377)
            await client.ping()


async def test_arg_from_environ(df_factory):
    with EnvironCntx(DFLY_requirepass="pass"):
        with df_factory.create() as dfly:
            # Expect password from environment variable
            with pytest.raises(redis.exceptions.AuthenticationError):
                client = aioredis.Redis(port=dfly.port)
                await client.ping()

            client = aioredis.Redis(password="pass", port=dfly.port)
            await client.ping()


async def test_unknown_dfly_env(df_factory, export_dfly_password):
    with EnvironCntx(DFLY_abcdef="xyz"):
        dfly = df_factory.create()
        with pytest.raises(DflyStartException):
            dfly.start()
        dfly.set_proc_to_none()


async def test_restricted_commands(df_factory):
    # Restrict GET and SET, then verify non-admin clients are blocked from
    # using these commands, though admin clients can use them.
    with df_factory.create(restricted_commands="get,set", admin_port=1112) as server:
        async with aioredis.Redis(port=server.port) as client:
            with pytest.raises(redis.exceptions.ResponseError):
                await client.get("foo")

            with pytest.raises(redis.exceptions.ResponseError):
                await client.set("foo", "bar")

        async with aioredis.Redis(port=server.admin_port) as admin_client:
            await admin_client.get("foo")
            await admin_client.set("foo", "bar")


@pytest.mark.asyncio
async def test_reply_guard_oom(df_factory, df_seeder_factory):
    master = df_factory.create(
        proactor_threads=1,
        cache_mode="true",
        maxmemory="256mb",
        enable_heartbeat_eviction="false",
        rss_oom_deny_ratio=2,
    )
    df_factory.start_all([master])
    c_master = master.client()
    await c_master.execute_command("DEBUG POPULATE 6000 size 40000")

    seeder = df_seeder_factory.create(
        port=master.port, keys=5000, val_size=1000, stop_on_failure=False
    )
    await seeder.run(target_deviation=0.1)

    info = await c_master.info("stats")
    assert info["evicted_keys"] > 0, f"Weak testcase: policy based eviction was not triggered."
