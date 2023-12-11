// Copyright 2022, DragonflyDB authors.  All rights reserved.
// See LICENSE for licensing terms.
//

#pragma once

#include <optional>
#include <shared_mutex>
#include <string_view>

#include "base/ring_buffer.h"
#include "server/common.h"
#include "server/journal/types.h"

namespace dfly {
namespace journal {

// Journal slice is present for both shards and io threads.
class JournalSlice {
 public:
  JournalSlice();
  ~JournalSlice();

  void Init(unsigned index);

#if 0
  std::error_code Open(std::string_view dir);

  std::error_code Close();
#endif

  // This is always the LSN of the *next* journal entry.
  LSN cur_lsn() const {
    return lsn_;
  }

  std::error_code status() const {
    return status_ec_;
  }

  // Whether the journaling is open.
  bool IsOpen() const {
    return slice_index_ != UINT32_MAX;
  }

  void AddLogRecord(const Entry& entry, bool await);

  // Register a callback that will be called every time a new entry is
  // added to the journal.
  // The callback receives the entry and a boolean that indicates whether
  // awaiting (to apply backpressure) is allowed.
  uint32_t RegisterOnChange(ChangeCallback cb);
  void UnregisterOnChange(uint32_t);

  bool HasRegisteredCallbacks() const {
    std::shared_lock lk(cb_mu_);
    return !change_cb_arr_.empty();
  }

  /// Returns whether the journal entry with this LSN is available
  /// from the buffer.
  bool IsLSNInBuffer(LSN lsn) const;
  std::string_view GetEntry(LSN lsn) const;

 private:
  // std::string shard_path_;
  // std::unique_ptr<LinuxFile> shard_file_;
  std::optional<base::RingBuffer<JournalItem>> ring_buffer_;
  base::IoBuf ring_serialize_buf_;

  mutable util::SharedMutex cb_mu_;
  std::vector<std::pair<uint32_t, ChangeCallback>> change_cb_arr_ ABSL_GUARDED_BY(cb_mu_);

  LSN lsn_ = 1;

  uint32_t slice_index_ = UINT32_MAX;
  uint32_t next_cb_id_ = 1;
  std::error_code status_ec_;
};

}  // namespace journal
}  // namespace dfly
