#include "core/editor_sync.hpp"

#include "TextEditor.h"
#include "log.h"
#include "network/client.hpp"

#include <chrono>

namespace core::editor_sync {

void processOutgoingSync(AppState& state, editor::EditorModule& module) {
    if (!state.collab.client || !state.collab.connected || !module.textEditor || state.collab.suppressOutgoingSync) {
        return;
    }

    if (!state.editorDirty) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - state.lastEditAt < std::chrono::milliseconds(state.collab.syncDebounceMs)) {
        return;
    }

    const std::string text = module.textEditor->GetText();
    if (text == state.collab.lastSyncedContent) {
        return;
    }

    std::string error;
    if (state.collab.client->send({{"type", "sync"}, {"content", text}}, error)) {
        state.collab.lastSyncedContent = text;
        state.collab.lastSyncSentAt = now;
        log(LogLevel::INFO, "sync sent");
    } else {
        state.status = "Sync send failed: " + error;
        state.clientState = AppState::ClientState::ERROR;
        log(LogLevel::ERROR, "sync send failed: " + error);
    }
}

}  // namespace core::editor_sync
