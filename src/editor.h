#pragma once

#include "app_state.h"

#include <filesystem>
#include <imgui.h>
#include <string>
#include <vector>

class TextEditor;

namespace editor {

struct Preferences {
    float fontSize = 18.0f;
    int selectedFontIndex = 0;
    bool showLineNumbers = true;
    bool showWhitespace = false;
    int tabSize = 4;
    ImVec4 keywordColor = ImVec4(0.86f, 0.54f, 0.24f, 1.0f);
    ImVec4 commentColor = ImVec4(0.33f, 0.71f, 0.43f, 1.0f);
    ImVec4 stringColor = ImVec4(0.82f, 0.79f, 0.44f, 1.0f);
    ImVec4 backgroundColor = ImVec4(0.10f, 0.11f, 0.13f, 1.0f);
    ImVec4 textColor = ImVec4(0.91f, 0.92f, 0.94f, 1.0f);
};

struct FontOption {
    std::string label;
    std::filesystem::path path;
};

struct EditorModule {
    TextEditor* textEditor = nullptr;
    Preferences preferences;
    std::vector<FontOption> fontOptions;
    std::vector<ImFont*> loadedFonts;
    int activeFontIndex = -1;
    bool fontAtlasDirty = false;
    bool initialized = false;
};

void initialize(AppState& state, EditorModule& module, ImGuiIO& io);
void shutdown(EditorModule& module);
bool loadDocument(AppState& state, EditorModule& module, const std::filesystem::path& path, std::string& error);
bool saveDocument(AppState& state, const EditorModule& module, std::string& error);
void renderEditor(AppState& state, EditorModule& module, const ImVec2& size);
void applyPreferences(EditorModule& module);
void ensureFonts(EditorModule& module, ImGuiIO& io);

}  // namespace editor
