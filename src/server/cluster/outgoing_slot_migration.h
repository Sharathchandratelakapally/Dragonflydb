// Copyright 2024, DragonflyDB authors.  All rights reserved.
// See LICENSE for licensing terms.
//
#pragma once

#include "io/io.h"
#include "server/cluster/cluster_config.h"
#include "server/common.h"

namespace dfly {

namespace journal {
class Journal;
}

class RestoreStreamer;
class DbSlice;

// Whole slots migration process information
class OutgoingMigration {
 public:
  OutgoingMigration() = default;
  ~OutgoingMigration();
  OutgoingMigration(std::uint32_t flows_num, std::string ip, uint16_t port,
                    std::vector<ClusterConfig::SlotRange> slots, Context::ErrHandler err_handler);

  void StartFlow(DbSlice* slice, uint32_t sync_id, journal::Journal* journal, io::Sink* dest);

  MigrationState GetState();

  const std::string& GetHostIp() const {
    return host_ip;
  };
  uint16_t GetPort() const {
    return port;
  };

  // Flow manages state and data transfering for the corresponding shard
  class Flow;

 private:
  std::string host_ip;
  uint16_t port;
  std::vector<ClusterConfig::SlotRange> slots;
  Context cntx;
  mutable Mutex flows_mu_;
  std::vector<std::unique_ptr<Flow>> flows ABSL_GUARDED_BY(flows_mu_);
};

}  // namespace dfly
