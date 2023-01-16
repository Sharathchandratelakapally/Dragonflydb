
import pytest
import asyncio
import aioredis
import random
from itertools import chain, repeat

from .utility import *
from . import dfly_args


BASE_PORT = 1111

DISCONNECT_CRASH_FULL_SYNC = 0
DISCONNECT_CRASH_STABLE_SYNC = 1
DISCONNECT_NORMAL_STABLE_SYNC = 2

"""
Test full replication pipeline. Test full sync with streaming changes and stable state streaming.
"""

# 1. Number of master threads
# 2. Number of threads for each replica
# 3. Seeder config
replication_cases = [
    (8, [8], dict(keys=10_000, dbcount=4)),
    (6, [6, 6, 6], dict(keys=4_000, dbcount=4)),
    (8, [2, 2, 2, 2], dict(keys=4_000, dbcount=4)),
    (4, [8, 8], dict(keys=4_000, dbcount=4)),
    (4, [1] * 8, dict(keys=500, dbcount=2)),
    (1, [1], dict(keys=100, dbcount=2)),
]


@pytest.mark.asyncio
@pytest.mark.parametrize("t_master, t_replicas, seeder_config", replication_cases)
async def test_replication_all(df_local_factory, df_seeder_factory, t_master, t_replicas, seeder_config):
    master = df_local_factory.create(port=1111, proactor_threads=t_master)
    replicas = [
        df_local_factory.create(port=BASE_PORT+i+1, proactor_threads=t)
        for i, t in enumerate(t_replicas)
    ]

    # Start master
    master.start()

    # Fill master with test data
    seeder = df_seeder_factory.create(port=master.port, **seeder_config)
    await seeder.run(target_deviation=0.1)

    # Start replicas
    df_local_factory.start_all(replicas)

    c_replicas = [aioredis.Redis(port=replica.port) for replica in replicas]

    # Start data stream
    stream_task = asyncio.create_task(seeder.run(target_ops=3000))
    await asyncio.sleep(0.0)

    # Start replication
    async def run_replication(c_replica):
        await c_replica.execute_command("REPLICAOF localhost " + str(master.port))

    await asyncio.gather(*(asyncio.create_task(run_replication(c))
                           for c in c_replicas))

    # Wait for streaming to finish
    assert not stream_task.done(
    ), "Weak testcase. Increase number of streamed iterations to surpass full sync"
    await stream_task

    # Check data after full sync
    await asyncio.sleep(3.0)
    await check_data(seeder, replicas, c_replicas)

    # Stream more data in stable state
    await seeder.run(target_ops=2000)

    # Check data after stable state stream
    await asyncio.sleep(3.0)
    await check_data(seeder, replicas, c_replicas)

    # Issue lots of deletes
    # TODO: Enable after stable state is faster
    # seeder.target(100)
    # await seeder.run(target_deviation=0.1)

    # Check data after deletes
    # await asyncio.sleep(2.0)
    # await check_data(seeder, replicas, c_replicas)


async def check_data(seeder, replicas, c_replicas):
    capture = await seeder.capture()
    for (replica, c_replica) in zip(replicas, c_replicas):
        await wait_available_async(c_replica)
        assert await seeder.compare(capture, port=replica.port)


"""
Test disconnecting replicas during different phases while constantly streaming changes to master.

This test is targeted at the master cancellation mechanism that should qickly stop operations for a
disconnected replica.

Three types are tested:
1. Replicas crashing during full sync state
2. Replicas crashing during stable sync state
3. Replicas disconnecting normally with REPLICAOF NO ONE during stable state
"""

# 1. Number of master threads
# 2. Number of threads for each replica that crashes during full sync
# 3. Number of threads for each replica that crashes during stable sync
# 4. Number of threads for each replica that disconnects normally
# 5. Number of distinct keys that are constantly streamed
disconnect_cases = [
    # balanced
    (8, [4, 4], [4, 4], [4], 4_000),
    (4, [2] * 4, [2] * 4, [2, 2], 2_000),
    # full sync heavy
    (8, [4] * 4, [], [], 4_000),
    # stable state heavy
    (8, [], [4] * 4, [], 4_000),
    # disconnect only
    (8, [], [], [4] * 4, 4_000)
]


@pytest.mark.asyncio
@pytest.mark.parametrize("t_master, t_crash_fs, t_crash_ss, t_disonnect, n_keys", disconnect_cases)
async def test_disconnect_replica(df_local_factory, df_seeder_factory, t_master, t_crash_fs, t_crash_ss, t_disonnect, n_keys):
    master = df_local_factory.create(port=BASE_PORT, proactor_threads=t_master)
    replicas = [
        (df_local_factory.create(
            port=BASE_PORT+i+1, proactor_threads=t), crash_fs)
        for i, (t, crash_fs) in enumerate(
            chain(
                zip(t_crash_fs, repeat(DISCONNECT_CRASH_FULL_SYNC)),
                zip(t_crash_ss, repeat(DISCONNECT_CRASH_STABLE_SYNC)),
                zip(t_disonnect, repeat(DISCONNECT_NORMAL_STABLE_SYNC))
            )
        )
    ]

    # Start master
    master.start()
    c_master = aioredis.Redis(port=master.port, single_connection_client=True)

    # Start replicas and create clients
    df_local_factory.start_all([replica for replica, _ in replicas])

    c_replicas = [
        (replica, aioredis.Redis(port=replica.port), crash_type)
        for replica, crash_type in replicas
    ]

    def replicas_of_type(tfunc):
        return [
            args for args in c_replicas
            if tfunc(args[2])
        ]

    # Start data fill loop
    seeder = df_seeder_factory.create(port=master.port, keys=n_keys, dbcount=2)
    fill_task = asyncio.create_task(seeder.run())

    # Run full sync
    async def full_sync(replica, c_replica, crash_type):
        c_replica = aioredis.Redis(port=replica.port)
        await c_replica.execute_command("REPLICAOF localhost " + str(master.port))
        if crash_type == 0:
            await asyncio.sleep(random.random()/100+0.01)
            replica.stop(kill=True)
        else:
            await wait_available_async(c_replica)

    await asyncio.gather(*(full_sync(*args) for args in c_replicas))

    # Wait for master to stream a bit more
    await asyncio.sleep(0.1)

    # Check master survived full sync crashes
    assert await c_master.ping()

    # Check phase-2 replicas survived
    for _, c_replica, _ in replicas_of_type(lambda t: t > 0):
        assert await c_replica.ping()

    # Run stable state crashes
    async def stable_sync(replica, c_replica, crash_type):
        await asyncio.sleep(random.random() / 100)
        replica.stop(kill=True)

    await asyncio.gather(*(stable_sync(*args) for args
                           in replicas_of_type(lambda t: t == 1)))

    # Check master survived all crashes
    assert await c_master.ping()

    # Check phase 3 replica survived
    for _, c_replica, _ in replicas_of_type(lambda t: t > 1):
        assert await c_replica.ping()

    # Stop streaming
    seeder.stop()
    await fill_task

    # Check master survived all crashes
    assert await c_master.ping()

    # Check phase 3 replicas are up-to-date and there is no gap or lag
    await seeder.run(target_ops=2000)
    await asyncio.sleep(1.0)

    capture = await seeder.capture()
    for replica, _, _ in replicas_of_type(lambda t: t > 1):
        assert await seeder.compare(capture, port=replica.port)

    # Check disconnects
    async def disconnect(replica, c_replica, crash_type):
        await asyncio.sleep(random.random() / 100)
        await c_replica.execute_command("REPLICAOF NO ONE")

    await asyncio.gather(*(disconnect(*args) for args
                           in replicas_of_type(lambda t: t == 2)))

    await asyncio.sleep(0.5)

    # Check phase 3 replica survived
    for replica, c_replica, _ in replicas_of_type(lambda t: t == 2):
        assert await c_replica.ping()
        assert await seeder.compare(capture, port=replica.port)

    # Check master survived all disconnects
    assert await c_master.ping()


"""
Test stopping master during different phases.

This test is targeted at the replica cancellation mechanism that should quickly abort a failed operation
and revert to connection retry state.

Three types are tested:
1. Master crashing during full sync state
2. Master crashing in a random state.
3. Master crashing during stable sync state

"""

# 1. Number of master threads
# 2. Number of threads for each replica
# 3. Number of times a random crash happens
# 4. Number of keys transferred (the more, the higher the propability to not miss full sync)
master_crash_cases = [
    (6, [6], 3, 2_000),
    (4, [4, 4, 4], 3, 2_000),
]


@pytest.mark.asyncio
@pytest.mark.parametrize("t_master, t_replicas, n_random_crashes, n_keys", master_crash_cases)
async def test_disconnect_master(df_local_factory, df_seeder_factory, t_master, t_replicas, n_random_crashes, n_keys):
    master = df_local_factory.create(port=1111, proactor_threads=t_master)
    replicas = [
        df_local_factory.create(
            port=BASE_PORT+i+1, proactor_threads=t)
        for i, t in enumerate(t_replicas)
    ]

    df_local_factory.start_all(replicas)
    c_replicas = [aioredis.Redis(port=replica.port) for replica in replicas]

    seeder = df_seeder_factory.create(port=master.port, keys=n_keys, dbcount=2)

    async def crash_master_fs():
        await asyncio.sleep(random.random() / 10 + 0.1 * len(replicas))
        master.stop(kill=True)

    async def start_master():
        master.start()
        c_master = aioredis.Redis(port=master.port)
        assert await c_master.ping()
        seeder.reset()
        await seeder.run(target_deviation=0.1)

    await start_master()

    # Crash master during full sync, but with all passing initial connection phase
    await asyncio.gather(*(c_replica.execute_command("REPLICAOF localhost " + str(master.port))
                           for c_replica in c_replicas), crash_master_fs())

    await asyncio.sleep(1 + len(replicas) * 0.5)

    for _ in range(n_random_crashes):
        await start_master()
        await asyncio.sleep(random.random() + len(replicas) * random.random() / 10)
        # Crash master in some random state for each replica
        master.stop(kill=True)

    await start_master()
    await asyncio.sleep(1 + len(replicas) * 0.5)  # Replicas check every 500ms.
    capture = await seeder.capture()
    for replica, c_replica in zip(replicas, c_replicas):
        await wait_available_async(c_replica)
        assert await seeder.compare(capture, port=replica.port)

    # Crash master during stable state
    master.stop(kill=True)

    await start_master()
    await asyncio.sleep(1 + len(replicas) * 0.5)
    capture = await seeder.capture()
    for c_replica in c_replicas:
        await wait_available_async(c_replica)
        assert await seeder.compare(capture, port=replica.port)


"""
Test flushall command. Set data to master send flashall and set more data.
Check replica keys at the end.
"""


@pytest.mark.asyncio
async def test_flushall(df_local_factory):
    master = df_local_factory.create(port=BASE_PORT, proactor_threads=4)
    replica = df_local_factory.create(port=BASE_PORT+1, proactor_threads=2)

    master.start()
    replica.start()

    # Connect replica to master
    c_replica = aioredis.Redis(port=replica.port)
    await c_replica.execute_command(f"REPLICAOF localhost {master.port}")

    n_keys = 1000

    def gen_test_data(start, end):
        for i in range(start, end):
            yield f"key-{i}", f"value-{i}"

    c_master = aioredis.Redis(port=master.port)
    pipe = c_master.pipeline(transaction=False)
    # Set simple keys 0..n_keys on master
    batch_fill_data(client=pipe, gen=gen_test_data(0, n_keys), batch_size=3)
    # flushall
    pipe.flushall()
    # Set simple keys n_keys..n_keys*2 on master
    batch_fill_data(client=pipe, gen=gen_test_data(
        n_keys, n_keys*2), batch_size=3)

    await pipe.execute()

    # Wait for replica to receive them
    await asyncio.sleep(1)

    # Check replica keys 0..n_keys-1 dont exist
    pipe = c_replica.pipeline(transaction=False)
    for i in range(n_keys):
        pipe.get(f"key-{i}")
    vals = await pipe.execute()
    assert all(v is None for v in vals)

    # Check replica keys n_keys..n_keys*2-1 exist
    for i in range(n_keys, n_keys*2):
        pipe.get(f"key-{i}")
    vals = await pipe.execute()
    assert all(v is not None for v in vals)
