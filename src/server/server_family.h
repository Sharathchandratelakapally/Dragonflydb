// Copyright 2022, DragonflyDB authors.  All rights reserved.
// See LICENSE for licensing terms.
//

#pragma once

#include <optional>
#include <string>

#include "facade/conn_context.h"
#include "facade/dragonfly_listener.h"
#include "facade/redis_parser.h"
#include "server/channel_store.h"
#include "server/engine_shard_set.h"
#include "server/replica.h"
#include "server/server_state.h"

void SlowLogGet(dfly::CmdArgList args, dfly::ConnectionContext* cntx, dfly::Service& service,
                std::string_view sub_cmd);

namespace util {

class AcceptServer;
class ListenerInterface;
class HttpListenerBase;

}  // namespace util

namespace dfly {

namespace detail {

class SnapshotStorage;

}  // namespace detail

std::string GetPassword();

namespace journal {
class Journal;
}  // namespace journal

class ClusterFamily;
class ConnectionContext;
class CommandRegistry;
class DflyCmd;
class Service;
class ScriptMgr;

struct ReplicaRoleInfo {
  std::string address;
  uint32_t listening_port;
  std::string_view state;
  uint64_t lsn_lag;
};

struct ReplicationMemoryStats {
  size_t streamer_buf_capacity_bytes_ = 0;  // total capacities of streamer buffers
  size_t full_sync_buf_bytes_ = 0;          // total bytes used for full sync buffers
};

// Global peak stats recorded after aggregating metrics over all shards.
// Note that those values are only updated during GetMetrics calls.
struct PeakStats {
  size_t conn_dispatch_queue_bytes = 0;  // peak value of conn_stats.dispatch_queue_bytes
  size_t conn_read_buf_capacity = 0;     // peak of total read buf capcacities
};

// Aggregated metrics over multiple sources on all shards
struct Metrics {
  SliceEvents events;              // general keyspace stats
  std::vector<DbStats> db_stats;   // dbsize stats
  EngineShard::Stats shard_stats;  // per-shard stats

  facade::ConnectionStats conn_stats;  // client stats and buffer sizes
  TieredStats tiered_stats;            // stats for tiered storage
  SearchStats search_stats;
  ServerState::Stats coordinator_stats;  // stats on transaction running

  PeakStats peak_stats;

  size_t uptime = 0;
  size_t qps = 0;

  size_t heap_used_bytes = 0;
  size_t small_string_bytes = 0;
  uint32_t traverse_ttl_per_sec = 0;
  uint32_t delete_ttl_per_sec = 0;
  uint64_t fiber_switch_cnt = 0;
  uint64_t fiber_switch_delay_ns = 0;

  // Statistics about fibers running for a long time (more than 1ms).
  uint64_t fiber_longrun_cnt = 0;
  uint64_t fiber_longrun_ns = 0;

  std::map<std::string, std::pair<uint64_t, uint64_t>> cmd_stats_map;  // command call frequencies

  bool is_master = true;
  std::vector<ReplicaRoleInfo> replication_metrics;
};

struct LastSaveInfo {
  time_t save_time = 0;  // epoch time in seconds.
  uint32_t duration_sec = 0;
  std::string file_name;                                      //
  std::vector<std::pair<std::string_view, size_t>> freq_map;  // RDB_TYPE_xxx -> count mapping.
};

struct SnapshotSpec {
  std::string hour_spec;
  std::string minute_spec;
};

struct ReplicaOffsetInfo {
  std::string sync_id;
  std::vector<uint64_t> flow_offsets;
};

class ServerFamily {
 public:
  explicit ServerFamily(Service* service);
  ~ServerFamily();

  void Init(util::AcceptServer* acceptor, std::vector<facade::Listener*> listeners);
  void Register(CommandRegistry* registry);
  void Shutdown();

  void ShutdownCmd(CmdArgList args, ConnectionContext* cntx);

  Service& service() {
    return service_;
  }

  Metrics GetMetrics() const;

  ScriptMgr* script_mgr() {
    return script_mgr_.get();
  }

  const ScriptMgr* script_mgr() const {
    return script_mgr_.get();
  }

  void StatsMC(std::string_view section, facade::ConnectionContext* cntx);

  // if new_version is true, saves DF specific, non redis compatible snapshot.
  // if basename is not empty it will override dbfilename flag.
  GenericError DoSave(bool new_version, std::string_view basename, Transaction* transaction);

  // Calls DoSave with a default generated transaction and with the format
  // specified in --df_snapshot_format
  GenericError DoSave();

  // Burns down and destroy all the data from the database.
  // if kDbAll is passed, burns all the databases to the ground.
  std::error_code Drakarys(Transaction* transaction, DbIndex db_ind);

  std::shared_ptr<const LastSaveInfo> GetLastSaveInfo() const;

  // Load snapshot from file (.rdb file or summary.dfs file) and return
  // future with error_code.
  Future<GenericError> Load(const std::string& file_name);

  bool IsSaving() const {
    return is_saving_.load(std::memory_order_relaxed);
  }

  void ConfigureMetrics(util::HttpListenerBase* listener);

  void PauseReplication(bool pause);
  std::optional<ReplicaOffsetInfo> GetReplicaOffsetInfo();

  const std::string& master_id() const {
    return master_id_;
  }

  journal::Journal* journal() {
    return journal_.get();
  }

  DflyCmd* GetDflyCmd() const {
    return dfly_cmd_.get();
  }

  const std::vector<facade::Listener*>& GetListeners() const {
    return listeners_;
  }

  bool HasReplica() const;
  std::optional<Replica::Info> GetReplicaInfo() const;
  std::string GetReplicaMasterId() const;

  void OnClose(ConnectionContext* cntx);

  void BreakOnShutdown();

  void CancelBlockingCommands();

  // Wait until all current dispatches finish, returns true on success, false if timeout was reached
  bool AwaitCurrentDispatches(absl::Duration timeout, util::Connection* issuer);

  // Sets the server to replicate another instance. Does not flush the database beforehand!
  void Replicate(std::string_view host, std::string_view port);

 private:
  void JoinSnapshotSchedule();

  uint32_t shard_count() const {
    return shard_set->size();
  }

  void Auth(CmdArgList args, ConnectionContext* cntx);
  void Client(CmdArgList args, ConnectionContext* cntx);
  void ClientSetName(CmdArgList args, ConnectionContext* cntx);
  void ClientGetName(CmdArgList args, ConnectionContext* cntx);
  void ClientList(CmdArgList args, ConnectionContext* cntx);
  void ClientPause(CmdArgList args, ConnectionContext* cntx);
  void Config(CmdArgList args, ConnectionContext* cntx);
  void DbSize(CmdArgList args, ConnectionContext* cntx);
  void Debug(CmdArgList args, ConnectionContext* cntx);
  void Dfly(CmdArgList args, ConnectionContext* cntx);
  void Memory(CmdArgList args, ConnectionContext* cntx);
  void FlushDb(CmdArgList args, ConnectionContext* cntx);
  void FlushAll(CmdArgList args, ConnectionContext* cntx);
  void Info(CmdArgList args, ConnectionContext* cntx);
  void Hello(CmdArgList args, ConnectionContext* cntx);
  void LastSave(CmdArgList args, ConnectionContext* cntx);
  void Latency(CmdArgList args, ConnectionContext* cntx);
  void Psync(CmdArgList args, ConnectionContext* cntx);
  void ReplicaOf(CmdArgList args, ConnectionContext* cntx);
  void ReplTakeOver(CmdArgList args, ConnectionContext* cntx);
  void ReplConf(CmdArgList args, ConnectionContext* cntx);
  void Role(CmdArgList args, ConnectionContext* cntx);
  void Save(CmdArgList args, ConnectionContext* cntx);
  void Script(CmdArgList args, ConnectionContext* cntx);
  void Sync(CmdArgList args, ConnectionContext* cntx);
  void SlowLog(CmdArgList args, ConnectionContext* cntx);
  void Module(CmdArgList args, ConnectionContext* cntx);

  void SyncGeneric(std::string_view repl_master_id, uint64_t offs, ConnectionContext* cntx);

  enum ActionOnConnectionFail {
    kReturnOnError,        // if we fail to connect to master, return to err
    kContinueReplication,  // continue attempting to connect to master, regardless of initial
                           // failure
  };

  // REPLICAOF implementation. See arguments above
  void ReplicaOfInternal(std::string_view host, std::string_view port, ConnectionContext* cntx,
                         ActionOnConnectionFail on_error);

  // Returns the number of loaded keys if successful.
  io::Result<size_t> LoadRdb(const std::string& rdb_file);

  void SnapshotScheduling();

  Fiber snapshot_schedule_fb_;
  Future<GenericError> load_result_;

  uint32_t stats_caching_task_ = 0;
  Service& service_;

  util::AcceptServer* acceptor_ = nullptr;
  std::vector<facade::Listener*> listeners_;
  util::ProactorBase* pb_task_ = nullptr;

  mutable Mutex replicaof_mu_, save_mu_;
  std::shared_ptr<Replica> replica_ ABSL_GUARDED_BY(replicaof_mu_);

  std::unique_ptr<ScriptMgr> script_mgr_;
  std::unique_ptr<journal::Journal> journal_;
  std::unique_ptr<DflyCmd> dfly_cmd_;

  std::string master_id_;

  time_t start_time_ = 0;  // in seconds, epoch time.

  std::shared_ptr<LastSaveInfo> last_save_info_;  // protected by save_mu_;
  std::atomic_bool is_saving_{false};
  // If a save operation is currently in progress, calling this function will provide information
  // about the memory consumption during the save operation.
  std::function<size_t()> save_bytes_cb_ = nullptr;

  // Used to override save on shutdown behavior that is usually set
  // be --dbfilename.
  bool save_on_shutdown_{true};

  Done schedule_done_;
  std::unique_ptr<FiberQueueThreadPool> fq_threadpool_;
  std::shared_ptr<detail::SnapshotStorage> snapshot_storage_;

  mutable Mutex peak_stats_mu_;
  mutable PeakStats peak_stats_;
};

}  // namespace dfly
