#pragma once

#include "app_state.h"

#include "network/protocol.hpp"

namespace core::file_sync {

void applyServerMessage(AppState& state, const network::protocol::Json& message);

}  // namespace core::file_sync
