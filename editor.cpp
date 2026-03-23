#include "editor.h"

#include <fstream>
#include <imgui.h>

namespace {
constexpr std::size_t kEditorCapacity = 1 << 20;
}

TextEditorBuffer::TextEditorBuffer(std::filesystem::path path) : filePath_(std::move(path)) {
    buffer_.resize(kEditorCapacity, '\0');
}

bool TextEditorBuffer::load() {
    std::ifstream input(filePath_, std::ios::binary);
    if (!input) {
        state_.content = R"(\documentclass{article}
\usepackage{tikz}
\usetikzlibrary{external}
\tikzexternalize[prefix=tikz_cache/]

\begin{document}
Hello from the local LaTeX editor!

\begin{tikzpicture}
  \draw[blue, thick] (0,0) circle (1cm);
\end{tikzpicture}
\end{document}
)";
        syncStorage();
        return saveIfDirty();
    }

    state_.content.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    syncStorage();
    dirty_ = false;
    return true;
}

bool TextEditorBuffer::saveIfDirty() {
    if (!dirty_ && std::filesystem::exists(filePath_)) {
        return true;
    }

    std::ofstream output(filePath_, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }

    output.write(state_.content.data(), static_cast<std::streamsize>(state_.content.size()));
    dirty_ = false;
    return true;
}

bool TextEditorBuffer::draw(const char* label, const ImVec2& size) {
    const bool changed = ImGui::InputTextMultiline(
        label,
        buffer_.data(),
        buffer_.size(),
        size,
        ImGuiInputTextFlags_AllowTabInput);

    if (changed) {
        state_.content = buffer_.data();
        ++state_.version;
        dirty_ = true;
    }

    return changed;
}

void TextEditorBuffer::markClean() {
    dirty_ = false;
}

void TextEditorBuffer::syncStorage() {
    if (state_.content.size() + 1 > buffer_.size()) {
        buffer_.resize(state_.content.size() + 1024, '\0');
    }
    std::fill(buffer_.begin(), buffer_.end(), '\0');
    std::copy(state_.content.begin(), state_.content.end(), buffer_.begin());
    dirty_ = true;
}
