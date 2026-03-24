#pragma once

#include "app_state.h"
#include "editor.h"

namespace core::editor_sync {

void processOutgoingSync(AppState& state, editor::EditorModule& module);

}  // namespace core::editor_sync
