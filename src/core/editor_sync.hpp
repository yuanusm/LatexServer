#pragma once

#include "app_state.h"
#include "editor.h"

#include <cstddef>
#include <string>

namespace core::editor_sync {

struct Operation {
    enum Type { Insert, Delete } type = Insert;
    std::size_t position = 0;
    char value = '\0';
    std::string file_path;
};

void processOutgoingSync(AppState& state, editor::EditorModule& module);

}  // namespace core::editor_sync
