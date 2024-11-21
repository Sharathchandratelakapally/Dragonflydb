// Copyright 2024, DragonflyDB authors.  All rights reserved.
// See LICENSE for licensing terms.
//

#include "server/journal/cmd_serializer.h"

#include "server/container_utils.h"
#include "server/journal/serializer.h"
#include "server/rdb_save.h"

namespace dfly {

namespace {
using namespace std;

class CommandAggregator {
 public:
  using Callback = std::function<void(absl::Span<const string_view>)>;

  CommandAggregator(string_view key, Callback cb) : key_(key), cb_(cb) {
  }

  ~CommandAggregator() {
    CommitPending();
  }

  enum class CommitMode { kAuto, kNoCommit };
  void AddArg(string arg, CommitMode commit_mode = CommitMode::kAuto) {
    agg_bytes_ += arg.size();
    members_.push_back(std::move(arg));

    if (commit_mode != CommitMode::kNoCommit && agg_bytes_ >= serialization_max_chunk_size) {
      CommitPending();
    }
  }

 private:
  void CommitPending() {
    if (members_.empty()) {
      return;
    }

    args_.clear();
    args_.reserve(members_.size() + 1);
    args_.push_back(key_);
    for (string_view member : members_) {
      args_.push_back(member);
    }
    cb_(args_);
    members_.clear();
  }

  string_view key_;
  Callback cb_;
  vector<string> members_;
  absl::InlinedVector<string_view, 5> args_;
  size_t agg_bytes_ = 0;
};

}  // namespace

CmdSerializer::CmdSerializer(Callback cb) : cb_(std::move(cb)) {
}

void CmdSerializer::SerializeCommand(string_view cmd, absl::Span<const string_view> args) {
  journal::Entry entry(0,                     // txid
                       journal::Op::COMMAND,  // single command
                       0,                     // db index
                       1,                     // shard count
                       0,                     // slot-id, but it is ignored at this level
                       journal::Entry::Payload(cmd, ArgSlice(args)));

  // Serialize into a string
  io::StringSink cmd_sink;
  JournalWriter writer{&cmd_sink};
  writer.Write(entry);

  cb_(std::move(cmd_sink).str());
}

void CmdSerializer::SerializeStickIfNeeded(string_view key, const PrimeValue& pk) {
  if (!pk.IsSticky()) {
    return;
  }

  SerializeCommand("STICK", {key});
}

void CmdSerializer::SerializeExpireIfNeeded(string_view key, uint64_t expire_ms) {
  if (expire_ms == 0) {
    return;
  }

  SerializeCommand("PEXIRE", {key, absl::StrCat(expire_ms)});
}

void CmdSerializer::SerializeSet(string_view key, const PrimeValue& pv) {
  CommandAggregator aggregator(
      key, [&](absl::Span<const string_view> args) { SerializeCommand("SADD", args); });

  container_utils::IterateSet(pv, [&](container_utils::ContainerEntry ce) {
    aggregator.AddArg(ce.ToString());
    return true;
  });
}

void CmdSerializer::SerializeZSet(string_view key, const PrimeValue& pv) {
}

void CmdSerializer::SerializeHash(string_view key, const PrimeValue& pv) {
}

void CmdSerializer::SerializeList(string_view key, const PrimeValue& pv) {
}

void CmdSerializer::SerializeRestore(string_view key, const PrimeValue& pk, const PrimeValue& pv,
                                     uint64_t expire_ms) {
  absl::InlinedVector<string_view, 5> args;
  args.push_back(key);

  string expire_str = absl::StrCat(expire_ms);
  args.push_back(expire_str);

  io::StringSink value_dump_sink;
  SerializerBase::DumpObject(pv, &value_dump_sink);
  args.push_back(value_dump_sink.str());

  args.push_back("ABSTTL");  // Means expire string is since epoch

  if (pk.IsSticky()) {
    args.push_back("STICK");
  }

  SerializeCommand("RESTORE", args);
}

}  // namespace dfly
