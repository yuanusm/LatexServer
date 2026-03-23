#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <imgui.h>

struct DocumentState {
    std::string content;
    uint64_t version = 0;
};

class TextEditorBuffer {
public:
    explicit TextEditorBuffer(std::filesystem::path path = "main.tex");

    bool load();
    bool saveIfDirty();
    bool draw(const char* label, const ImVec2& size);

    const std::filesystem::path& path() const { return filePath_; }
    const DocumentState& state() const { return state_; }
    bool dirty() const { return dirty_; }
    void markClean();

private:
    void syncStorage();

    std::filesystem::path filePath_;
    DocumentState state_;
    std::vector<char> buffer_;
    bool dirty_ = false;
};
