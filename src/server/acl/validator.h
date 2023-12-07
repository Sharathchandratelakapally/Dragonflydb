// Copyright 2023, DragonflyDB authors.  All rights reserved.
// See LICENSE for licensing terms.
//

#pragma once

#include <utility>

#include "facade/command_id.h"
#include "server/acl/acl_log.h"
#include "server/conn_context.h"

namespace dfly::acl {

std::pair<bool, AclLog::Reason> IsUserAllowedToInvokeCommandGeneric(
    uint32_t acl_cat, const std::vector<uint64_t>& acl_commands, const AclKeys& keys,
    CmdArgList tail_args, const facade::CommandId& id);

bool IsUserAllowedToInvokeCommand(const ConnectionContext& cntx, const facade::CommandId& id,
                                  CmdArgList tail_args);

}  // namespace dfly::acl
