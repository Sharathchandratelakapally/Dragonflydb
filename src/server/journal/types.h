// Copyright 2022, Roman Gershman.  All rights reserved.
// See LICENSE for licensing terms.
//
#pragma once

#include <string>
#include <variant>

#include "server/common.h"
#include "server/table.h"

namespace dfly {
namespace journal {

enum class Op : uint8_t {
  NOOP = 0,
  LOCK = 1,
  UNLOCK = 2,
  LOCK_SHARD = 3,
  UNLOCK_SHARD = 4,
  SCHED = 5,
  SELECT = 6,
  VAL = 10,
  DEL,
  MSET,
};

// TODO: to pass all the attributes like ttl, stickiness etc.
struct Entry {
  Entry(Op op, DbIndex did, TxId tid, std::string_view skey)
      : opcode(op), db_ind(did), txid(tid), key(skey) {
  }

  Entry(DbIndex did, TxId tid, std::string_view skey, const PrimeValue& pval)
      : Entry(Op::VAL, did, tid, skey) {
    pval_ptr = &pval;
  }

  static Entry Sched(TxId tid) {
    return Entry{Op::SCHED, 0, tid, {}};
  }

  Op opcode;
  DbIndex db_ind;
  TxId txid;
  std::string_view key;
  const PrimeValue* pval_ptr = nullptr;
  uint64_t expire_ms = 0;  // 0 means no expiry.
};

struct EntryBase {
  TxId txid;
  Op opcode;
  DbIndex dbid;
};

// This struct represents a single journal entry.
// Those are either control instructions or commands.
struct EntryNew : public EntryBase {  // Called this "New" because I can't delete the old neither
                                      // replace it partially
  // Payload represents a non-owning view into a command executed on the shard.
  using Payload =
      std::variant<std::monostate,                        // No payload.
                   CmdArgList,                            // Parts of a full command.
                   std::pair<std::string_view, ArgSlice>  // Command and its shard parts.
                   >;

  EntryNew(TxId txid, DbIndex dbid, Payload pl)
      : EntryBase{txid, journal::Op::VAL, dbid}, payload{pl} {
  }

  EntryNew(journal::Op opcode, DbIndex dbid) : EntryBase{0, opcode, dbid}, payload{} {
  }

  Payload payload;
};

struct ParsedEntry : public EntryBase {
  using Payload = std::optional<CmdArgVec>;

  ParsedEntry(journal::Op opcode, DbIndex dbid) : EntryBase{0, opcode, dbid}, payload{} {
  }

  ParsedEntry(TxId txid, DbIndex dbid, Payload pl)
      : EntryBase{txid, journal::Op::VAL, dbid}, payload{pl} {
  }

  Payload payload;
};

using ChangeCallback = std::function<void(const Entry&)>;

}  // namespace journal
}  // namespace dfly
