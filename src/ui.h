#pragma once

#include "app_state.h"
#include "editor.h"
#include "file_tree.h"

namespace ui {

void renderMainWindow(AppState& state, editor::EditorModule& editorModule, file_tree::FileTreeModule& fileTreeModule);

}  // namespace ui
