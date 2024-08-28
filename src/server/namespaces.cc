#include "server/namespaces.h"

#include "base/flags.h"
#include "base/logging.h"
#include "server/common.h"
#include "server/engine_shard_set.h"

ABSL_DECLARE_FLAG(bool, cache_mode);

namespace dfly {

using namespace std;

Namespace::Namespace() {
  shard_db_slices_.resize(shard_set->size());
  shard_blocking_controller_.resize(shard_set->size());
  shard_set->RunBriefInParallel([&](EngineShard* es) {
    CHECK(es != nullptr);
    ShardId sid = es->shard_id();
    shard_db_slices_[sid] = make_unique<DbSlice>(sid, absl::GetFlag(FLAGS_cache_mode), es);
    shard_db_slices_[sid]->UpdateExpireBase(absl::GetCurrentTimeNanos() / 1000000, 0);
  });
}

DbSlice& Namespace::GetCurrentDbSlice() {
  EngineShard* es = EngineShard::tlocal();
  CHECK(es != nullptr);
  return GetDbSlice(es->shard_id());
}

DbSlice& Namespace::GetDbSlice(ShardId sid) {
  CHECK_LT(sid, shard_db_slices_.size());
  return *shard_db_slices_[sid];
}

BlockingController* Namespace::GetOrAddBlockingController(EngineShard* shard) {
  if (!shard_blocking_controller_[shard->shard_id()]) {
    shard_blocking_controller_[shard->shard_id()] = make_unique<BlockingController>(shard, this);
  }

  return shard_blocking_controller_[shard->shard_id()].get();
}

BlockingController* Namespace::GetBlockingController(ShardId sid) {
  return shard_blocking_controller_[sid].get();
}

Namespaces namespaces;

Namespaces::~Namespaces() {
  Clear();
}

void Namespaces::Init() {
  DCHECK(default_namespace_ == nullptr);
  default_namespace_ = &GetOrInsert("");
}

bool Namespaces::IsInitialized() const {
  return default_namespace_ != nullptr;
}

void Namespaces::Clear() {
  util::fb2::LockGuard guard(mu_);

  default_namespace_ = nullptr;

  if (namespaces_.empty()) {
    return;
  }

  shard_set->RunBriefInParallel([&](EngineShard* es) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    CHECK(es != nullptr);
    for (auto& ns : namespaces_) {
      ns.second.shard_db_slices_[es->shard_id()].reset();
    }
  });

  namespaces_.clear();
}

Namespace& Namespaces::GetDefaultNamespace() const {
  CHECK(default_namespace_ != nullptr);
  return *default_namespace_;
}

Namespace& Namespaces::GetOrInsert(std::string_view ns) {
  {
    // Try to look up under a shared lock
    dfly::SharedLock guard(mu_);
    auto it = namespaces_.find(ns);
    if (it != namespaces_.end()) {
      return it->second;
    }
  }

  {
    // Key was not found, so we create create it under unique lock
    util::fb2::LockGuard guard(mu_);
    return namespaces_[ns];
  }
}

}  // namespace dfly
