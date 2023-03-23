<p align="center">
  <a href="https://dragonflydb.io">
    <img  src="/.github/images/logo-full.svg"
      width="284" border="0" alt="Dragonfly">
  </a>
</p>

[![ci-tests](https://github.com/dragonflydb/dragonfly/actions/workflows/ci.yml/badge.svg)](https://github.com/dragonflydb/dragonfly/actions/workflows/ci.yml) [![Twitter URL](https://img.shields.io/twitter/follow/dragonflydbio?style=social)](https://twitter.com/dragonflydbio)


[Website](https://dragonflydb.io/) • [Quick Start](https://github.com/dragonflydb/dragonfly/tree/main/docs/quick-start) • [Community Discord](https://discord.gg/HsPjXGVH85) • [GitHub Discussions](https://github.com/dragonflydb/dragonfly/discussions) | [GitHub Issues](https://github.com/dragonflydb/dragonfly/issues) | [Contributing](https://github.com/dragonflydb/dragonfly/blob/main/CONTRIBUTING.md)

## The world's fastest in-memory data store

Dragonfly is an in-memory data store built for modern application workloads. 

Fully compatible with Redis and Memcached APIs, Dragonfly requires no code changes to adopt. Compared to legacy in-memory datastores, Dragonfly delivers 25X more throughput, higher cache hit rates with lower tail latency, and effortless vertical scalability.

## Contents

- [Benchmarks](#benchmarks)
- [Quick start](https://github.com/dragonflydb/dragonfly/tree/main/docs/quick-start)
- [Configuration](#configuration)
- [Roadmap and status](#roadmap-status)
- [Design decisions](#design-decisions)
- [Background](#background)

## <a name="benchmarks"><a/>Benchmarks

<img src="http://static.dragonflydb.io/repo-assets/aws-throughput.svg" width="80%" border="0"/>

In benchmarks, Dragonfly showed a 25X increase in throughput compared to Redis, crossing 3.8M QPS on c6gn.16xlarge.

Dragonfly's 99th percentile latency metrics at its peak throughput:

| op  |r6g | c6gn | c7g |
|-----|-----|------|----|
| set |0.8ms  | 1ms | 1ms   |
| get | 0.9ms | 0.9ms |0.8ms |
|setex| 0.9ms | 1.1ms | 1.3ms


*All benchmarks were performed using `memtier_benchmark` (see below) with number of threads tuned per server and instance type. `memtier` was run on a separate c6gn.16xlarge machine. We set the expiry time to 500 for the SETEX benchmark to ensure it would survive the end of the test.*

```bash
  memtier_benchmark --ratio ... -t <threads> -c 30 -n 200000 --distinct-client-seed -d 256 \
     --expiry-range=...
```

In pipeline mode `--pipeline=30`, Dragonfly reaches **10M QPS** for SET and **15M QPS** for GET operations.

### Dragonfly vs. Memcached

We compared Dragonfly with Memcached on a c6gn.16xlarge instance on AWS.

With a comparable latency, Dragonfly throughput outperformed Memcached throughput in both write and read workloads. Dragonfly demonstrated better latency in write workloads due to contention on the [write path in Memcached](docs/memcached_benchmark.md).

#### SET benchmark

| Server    | QPS(thousands qps) | latency 99% | 99.9%   |
|:---------:|:------------------:|:-----------:|:-------:|
| Dragonfly |  🟩 3844           |🟩 0.9ms     | 🟩 2.4ms |
| Memcached |   806              |   1.6ms     | 3.2ms    |

#### GET benchmark

| Server    | QPS(thousands qps) | latency 99% | 99.9%   |
|-----------|:------------------:|:-----------:|:-------:|
| Dragonfly | 🟩 3717            |   1ms       | 2.4ms   |
| Memcached |   2100             |  🟩 0.34ms  | 🟩 0.6ms |


Memcached exhibited lower latency for the read benchmark, but also lower throughput.

### Memory efficiency

To test memory efficiency, we filled Dragonfly and Redis with ~5GB of data using the `debug populate 5000000 key 1024` command, sent update traffic with `memtier`, and kicked off the snapshotting with the `bgsave` command. 
  
This figure demonstrates how each server behaved in terms of memory efficiency.

<img src="http://static.dragonflydb.io/repo-assets/bgsave-memusage.svg" width="70%" border="0"/>

Dragonfly was 30% more memory efficient than Redis in the idle state and did not show any visible increase in memory use during the snapshot phase. At peak, Redis memory use increased to almost 3X that of Dragonfly.

Dragonfly finished the snapshot faster, within a few seconds.
  
For more info about memory efficiency in Dragonfly, see our [Dashtable doc](/docs/dashtable.md).



## <a name="configuration"><a/>Configuration
  
Dragonfly supports common Redis arguments where applicable. For example, you can run: `dragonfly --requirepass=foo --bind localhost`.

Dragonfly currently supports the following Redis-specific arguments:
 * `port`: Redis connection port (`default: 6379`).
 * `bind`: Use `localhost` to only allow localhost connections or a public IP address to allow connections **to that IP** address (i.e. from outside too).
 * `requirepass`: The password for AUTH authentication (`default: ""`).
 * `maxmemory`: Limit on maximum memory (in human-readable bytes) used by the database (`default: 0`). A `maxmemory` value of `0` means the program will automatically determine its maximum memory usage.
 * `dir`: Dragonfly Docker uses the `/data` folder for snapshotting by default, the CLI uses `""`. You can use the `-v` Docker option to map it to your host folder.
 * `dbfilename`: The filename to save and load the database (`default: dump`).

There are also some Dragonfly-specific arguments:
 * `memcache_port`: The port to enable Memcached-compatible API on (`default: disabled`).
 * `keys_output_limit`: Maximum number of returned keys in `keys` command (`default: 8192`). Note that `keys` is a dangerous command. We truncate its result to avoid a blowup in memory use when fetching too many keys.
 * `dbnum`: Maximum number of supported databases for `select`.
 * `cache_mode`: See the [novel cache design](#novel-cache-design) section below.
 * `hz`: Key expiry evaluation frequency (`default: 100`). Lower frequency uses less CPU when idle at the expense of a slower eviction rate.
 * `save_schedule`: Glob spec for the UTC to save a snapshot in HH:MM (24h time) format (`default: ""`).
 * `primary_port_http_enabled`: Allows accessing HTTP console on main TCP port if `true` (`default: true`).
 * `admin_port`: To enable admin access to the console on the assigned port (`default: disabled`). Supports both HTTP and RESP protocols.
 * `admin_bind`: To bind the admin console TCP connection to a given address (`default: any`). Supports both HTTP and RESP protocols. 
 * `cluster_mode`: Cluster mode supported (`default: ""`). Currently supports only `emulated`. 
 * `cluster_announce_ip`: The IP that cluster commands announce to the client.

### Example start script with popular options:

```bash
./dragonfly-x86_64 --logtostderr --requirepass=youshallnotpass --cache_mode=true -dbnum 1 --bind localhost --port 6379  --save_schedule "*:30" --maxmemory=12gb --keys_output_limit=12288 --dbfilename dump.rdb
```

For more options like logs management or TLS support, run `dragonfly --help`.

## <a name="roadmap-status"><a/>Roadmap and status

Dragonfly currently supports ~185 Redis commands and all Memcache commands besides `cas`. Almost on par with the Redis 5 API, Dragonfly's next milestone will be to stabilize basic functionality and implement the replication API. If there is a command you need that is not implemented yet, please open an issue.

For Dragonfly-native replication, we are designing a distributed log format that will support exponentially higher speeds.

Following the replication feature, we will continue adding missing commands for Redis versions 3-6 APIs.

Please see our [Command Reference](https://dragonflydb.io/docs/category/command-reference) for the current commands supported by Dragonfly.

## <a name="design-decisions"><a/> Design decisions

### Novel cache design
  
Dragonfly has a single, unified, adaptive caching algorithm that is simple and memory efficient.

You can enable caching mode by passing the `--cache_mode=true` flag. Once this mode is on, Dragonfly will evict items least likely to be stumbled upon in the future but only when it is near the `maxmemory` limit.

### Expiration deadlines with relative accuracy
  
Expiration ranges are limited to ~4 years. 
  
Expiration deadlines with millisecond precision (PEXPIRE, PSETEX, etc.) are rounded to the closest second **for deadlines greater than 134217727ms (approximately 37 hours)**, which has less than 0.001% error and should be acceptable for large ranges. If this is not suitable for your use case, get in touch or open an issue explaining your case.

For more detailed differences between Dragonfly expiration deadlines and Redis implementations, [see here](docs/differences.md).

### Native HTTP console and Prometheus-compatible metrics
  
By default, Dragonfly allows HTTP access via its main TCP port (6379). That's right, you can connect to Dragonfly via Redis protocol and via HTTP protocol — the server recognizes the protocol automatically during the connection initiation. Go ahead and try it with your browser. HTTP access currently does not have much info but will include useful debugging and management info in the future. 
  
Go to the URL `:6379/metrics` to view Prometheus-compatible metrics.

The Prometheus exported metrics are compatible with the Grafana dashboard, [see here](tools/local/monitoring/grafana/provisioning/dashboards/dashboard.json).


Important! The HTTP console is meant to be accessed within a safe network. If you expose Dragonfly's TCP port externally, we advise you disable the console with `--http_admin_console=false` or `--nohttp_admin_console`.


## <a name="background"><a/>Background

Dragonfly started as an experiment to see how an in-memory datastore could look if it was designed in 2022. Based on lessons learned from our experience as users of memory stores and engineers who worked for cloud companies, we knew that we need to preserve two key properties for Dragonfly: Atomicity guarantees for all operations and low, sub-millisecond latency over very high throughput.

Our first challenge was how to fully utilize CPU, memory, and I/O resources using servers that are available today in public clouds. To solve this, we use [shared-nothing architecture](https://en.wikipedia.org/wiki/Shared-nothing_architecture), which allows us to partition the keyspace of the memory store between threads so that each thread can manage its own slice of dictionary data. We call these slices "shards". The library that powers thread and I/O management for shared-nothing architecture is open-sourced [here](https://github.com/romange/helio).

To provide atomicity guarantees for multi-key operations, we use the advancements from recent academic research. We chose the paper ["VLL: a lock manager redesign for main memory database systems”](https://www.cs.umd.edu/~abadi/papers/vldbj-vll.pdf) to develop the transactional framework for Dragonfly. The choice of shared-nothing architecture and VLL allowed us to compose atomic multi-key operations without using mutexes or spinlocks. This was a major milestone for our PoC and its performance stood out from other commercial and open-source solutions.

Our second challenge was to engineer more efficient data structures for the new store. To achieve this goal, we based our core hashtable structure on the paper ["Dash: Scalable Hashing on Persistent Memory"](https://arxiv.org/pdf/2003.07302.pdf). The paper itself is centered around the persistent memory domain and is not directly related to main-memory stores, but it's still most applicable to our problem. The hashtable design suggested in the paper allowed us to maintain two special properties that are present in the Redis dictionary: The incremental hashing ability during datastore growth the ability to traverse the dictionary under changes using a stateless scan operation. In addition to these two properties, Dash is more efficient in CPU and memory use. By leveraging Dash's design, we were able to innovate further with the following features:
 * Efficient record expiry for TTL records.
 * A novel cache eviction algorithm that achieves higher hit rates than other caching strategies like LRU and LFU with **zero memory overhead**.
 * A novel **fork-less** snapshotting algorithm.

Once we had built the foundation for Dragonfly and [we were happy with its performance](#benchmarks), we went on to implement the Redis and Memcached functionality. We have to date implemented ~185 Redis commands (roughly equivalent to Redis 5.0 API) and 13 Memcached commands.

And finally, <br>
<em>Our mission is to build a well-designed, ultra-fast, cost-efficient in-memory datastore for cloud workloads that takes advantage of the latest hardware advancements. We intend to address the pain points of current solutions while preserving their product APIs and propositions.
</em>
