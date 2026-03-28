#include "editor.h"

#include <cstdint>

#include "TextEditor.h"
#include "compiler.h"
#include "network/client.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace editor {
namespace {

std::unique_ptr<TextEditor> gEditor;

struct CompletionItem {
    std::string label;
    std::string insertText;
    int cursorOffset = 0;
    bool isTemplate = false;
};

const std::vector<CompletionItem> kLatexCommands = {
    {"\\section", "\\section{}", 9, false},
    {"\\subsection", "\\subsection{}", 12, false},
    {"\\paragraph", "\\paragraph{}", 11, false},
    {"\\begin", "\\begin{}", 7, false},
    {"\\end", "\\end{}", 5, false},
    {"\\frac", "\\frac{}{}", 6, false},
    {"\\sum", "\\sum_{}^{}", 6, false},
    {"\\int", "\\int_{}^{}", 6, false},
    {"\\sqrt", "\\sqrt{}", 6, false},
    {"\\alpha", "\\alpha", 6, false},
    {"\\beta", "\\beta", 5, false},
    {"\\textbf", "\\textbf{}", 8, false},
    {"\\textit", "\\textit{}", 8, false},
    {"\\includegraphics", "\\includegraphics[]{}", 17, false},
    {"\\label", "\\label{}", 7, false},
    {"\\ref", "\\ref{}", 5, false},
    {"\\cite", "\\cite{}", 6, false},
    {"\\item", "\\item ", 6, false},
};

const std::vector<CompletionItem> kLatexTemplates = {
    {"Template: itemize", "\\begin{itemize}\n    \\item \n\\end{itemize}", 24, true},
    {"Template: enumerate", "\\begin{enumerate}\n    \\item \n\\end{enumerate}", 26, true},
    {"Template: figure", "\\begin{figure}[h]\n    \\centering\n    \\includegraphics[width=0.8\\\\textwidth]{}\n    \\caption{}\n    \\label{fig:}\n\\end{figure}", 78, true},
    {"Template: equation", "\\begin{equation}\n    \n\\end{equation}", 22, true},
    {"Template: align", "\\begin{align}\n    \n\\end{align}", 19, true},
    {"Template: fraction", "\\frac{}{}", 6, true},
    {"Template: sqrt", "\\sqrt{}", 6, true},
    {"Template: sum", "\\sum_{i=1}^{n}", 7, true},
    {"Template: integral", "\\int_{a}^{b}", 7, true},
    {"Template: table", "\\begin{table}[h]\n    \\centering\n    \\begin{tabular}{|c|c|}\n        \\hline\n         &  \\\\\n        \\hline\n    \\end{tabular}\n    \\caption{}\n    \\label{tab:}\n\\end{table}", 98, true},
};

bool extractCompletionPrefix(const std::string& text, std::size_t& slashPos, std::string& prefix) {
    slashPos = std::string::npos;
    prefix.clear();
    if (text.empty()) {
        return false;
    }
    std::size_t idx = text.size();
    while (idx > 0 && std::isalpha(static_cast<unsigned char>(text[idx - 1]))) {
        --idx;
    }
    if (idx == text.size()) {
        return false;
    }
    if (idx == 0 || text[idx - 1] != '\\') {
        return false;
    }
    slashPos = idx - 1;
    prefix = text.substr(idx);
    return !prefix.empty();
}

void renderAutocomplete(AppState& state, EditorModule& module) {
    if (!module.textEditor) {
        return;
    }
    const auto cursor = module.textEditor->GetCursorPosition();
    const std::string allText = module.textEditor->GetText();
    std::size_t byteIndex = 0;
    int line = 0;
    int column = 0;
    for (; byteIndex < allText.size(); ++byteIndex) {
        if (line == cursor.mLine && column == cursor.mColumn) {
            break;
        }
        if (allText[byteIndex] == '\n') {
            ++line;
            column = 0;
        } else {
            ++column;
        }
    }
    const std::string prefixText = allText.substr(0, byteIndex);
    std::size_t slashPos = std::string::npos;
    std::string prefix;
    if (!extractCompletionPrefix(prefixText, slashPos, prefix)) {
        ImGui::CloseCurrentPopup();
        return;
    }

    std::vector<CompletionItem> commandMatches;
    for (const auto& item : kLatexCommands) {
        if (item.label.size() > 1 && item.label.rfind("\\" + prefix, 0) == 0) {
            commandMatches.push_back(item);
        }
    }
    const bool showPopup = !commandMatches.empty() || !kLatexTemplates.empty();
    if (!showPopup) {
        return;
    }

    ImGui::SetNextWindowBgAlpha(0.96f);
    if (ImGui::Begin("Autocomplete##Latex", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Commands");
        for (const auto& match : commandMatches) {
            if (ImGui::Selectable(match.label.c_str())) {
                const std::string replaced =
                    allText.substr(0, slashPos) + match.insertText + allText.substr(byteIndex);
                module.textEditor->SetText(replaced);
                const std::size_t target = slashPos + static_cast<std::size_t>(match.cursorOffset);
                int targetLine = 0;
                int targetCol = 0;
                for (std::size_t i = 0; i < std::min(target, replaced.size()); ++i) {
                    if (replaced[i] == '\n') {
                        ++targetLine;
                        targetCol = 0;
                    } else {
                        ++targetCol;
                    }
                }
                module.textEditor->SetCursorPosition({targetLine, targetCol});
                state.editorDirty = true;
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Templates");
        for (const auto& item : kLatexTemplates) {
            if (ImGui::Selectable(item.label.c_str())) {
                const std::string replaced =
                    allText.substr(0, byteIndex) + item.insertText + allText.substr(byteIndex);
                module.textEditor->SetText(replaced);
                const std::size_t target = byteIndex + static_cast<std::size_t>(item.cursorOffset);
                int targetLine = 0;
                int targetCol = 0;
                for (std::size_t i = 0; i < std::min(target, replaced.size()); ++i) {
                    if (replaced[i] == '\n') {
                        ++targetLine;
                        targetCol = 0;
                    } else {
                        ++targetCol;
                    }
                }
                module.textEditor->SetCursorPosition({targetLine, targetCol});
                state.editorDirty = true;
            }
        }
    }
    ImGui::End();
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::vector<FontOption> discoverFonts() {
    std::vector<FontOption> fonts;
    const std::vector<fs::path> roots = {
        fs::path{"C:/Windows/Fonts"},
        fs::path{"/usr/share/fonts"},
        fs::path{"/usr/local/share/fonts"},
        fs::path{std::getenv("HOME") ? std::getenv("HOME") : ""} / ".fonts"
    };

    std::set<std::string> seen;
    for (const fs::path& root : roots) {
        std::error_code ec;
        if (root.empty() || !fs::exists(root, ec)) {
            continue;
        }
        for (fs::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
            if (!it->is_regular_file()) {
                continue;
            }
            const std::string ext = toLower(it->path().extension().string());
            if (ext != ".ttf" && ext != ".otf") {
                continue;
            }
            const std::string label = it->path().stem().string();
            if (seen.insert(label).second) {
                fonts.push_back({label, it->path()});
            }
            if (fonts.size() >= 24) {
                return fonts;
            }
        }
    }

    if (fonts.empty()) {
        fonts.push_back({"Default ImGui Font", {}});
    } else {
        std::sort(fonts.begin(), fonts.end(), [](const FontOption& a, const FontOption& b) {
            return a.label < b.label;
        });
        fonts.insert(fonts.begin(), {"Default ImGui Font", {}});
    }
    return fonts;
}

TextEditor::LanguageDefinition makeLatexLanguageDefinition() {
    TextEditor::LanguageDefinition language;
    language.mName = "LaTeX";
    language.mCaseSensitive = true;
    language.mAutoIndentation = true;
    language.mSingleLineComment = "%";
    language.mCommentStart = "%";
    language.mCommentEnd.clear();

    language.mKeywords = {
        "\\begin", "\\end", "\\section", "\\subsection", "\\subsubsection",
        "\\chapter", "\\part", "\\paragraph", "\\subparagraph", "\\textbf",
        "\\textit", "\\emph", "\\underline", "\\item", "\\itemize",
        "\\enumerate", "\\documentclass", "\\usepackage", "\\title", "\\author",
        "\\date", "\\maketitle", "\\tableofcontents", "\\label", "\\ref",
        "\\pageref", "\\cite", "\\footnote", "\\caption", "\\includegraphics",
        "\\centering", "\\frac", "\\sqrt", "\\sum", "\\int", "\\alpha",
        "\\beta", "\\gamma", "\\delta", "\\epsilon", "\\lambda", "\\mu",
        "\\pi", "\\sigma", "\\phi", "\\omega", "\\mathbf", "\\mathit",
        "\\mathrm", "\\mathbb", "\\left", "\\right", "\\bigl", "\\bigr",
        "\\displaystyle", "\\text", "\\newline", "\\[", "\\]", "\\(", "\\)"
    };

    using PaletteIndex = TextEditor::PaletteIndex;
    language.mTokenRegexStrings.push_back({R"((\\[A-Za-z@]+))", PaletteIndex::Keyword});
    language.mTokenRegexStrings.push_back({"[a-zA-Z]+", PaletteIndex::Keyword});
    language.mTokenRegexStrings.push_back({R"((\\[^A-Za-z\s]))", PaletteIndex::Keyword});
    language.mTokenRegexStrings.push_back({R"((\$\$[^$]*\$\$))", PaletteIndex::String});
    language.mTokenRegexStrings.push_back({R"((\$[^$\n]+\$))", PaletteIndex::String});
    language.mTokenRegexStrings.push_back({R"((\\\([^\n]*\\\)))", PaletteIndex::String});
    language.mTokenRegexStrings.push_back({R"((\\\[[^\n]*\\\]))", PaletteIndex::String});
    language.mTokenRegexStrings.push_back({R"((\{[^\}\n]*\}))", PaletteIndex::String});
    language.mTokenRegexStrings.push_back({R"(([0-9]+))", PaletteIndex::Number});

    return language;
}

TextEditor::Palette makePalette(const Preferences& preferences) {
    auto palette = TextEditor::GetDarkPalette();
    const auto slot = [&palette](TextEditor::PaletteIndex index) -> ImU32& {
        return palette[static_cast<std::size_t>(index)];
    };

    slot(TextEditor::PaletteIndex::Keyword) = ImColor(preferences.keywordColor);
    slot(TextEditor::PaletteIndex::Comment) = ImColor(preferences.commentColor);
    slot(TextEditor::PaletteIndex::String) = ImColor(preferences.stringColor);
    slot(TextEditor::PaletteIndex::Background) = ImColor(preferences.backgroundColor);
    slot(TextEditor::PaletteIndex::Default) = ImColor(preferences.textColor);
    slot(TextEditor::PaletteIndex::LineNumber) = preferences.showLineNumbers
        ? ImColor(0.55f, 0.58f, 0.64f, 1.0f)
        : ImColor(preferences.backgroundColor);
    slot(TextEditor::PaletteIndex::CurrentLineFill) = ImColor(0.16f, 0.18f, 0.22f, 1.0f);
    slot(TextEditor::PaletteIndex::CurrentLineFillInactive) = ImColor(0.13f, 0.14f, 0.18f, 1.0f);
    return palette;
}

}  // namespace

void initialize(AppState& state, EditorModule& module, ImGuiIO& io) {
    if (!gEditor) {
        gEditor = std::make_unique<TextEditor>();
    }

    module.textEditor = gEditor.get();
    module.fontOptions = discoverFonts();
    ensureFonts(module, io);

    module.textEditor->SetLanguageDefinition(makeLatexLanguageDefinition());
    module.textEditor->SetPalette(makePalette(module.preferences));
    module.textEditor->SetShowWhitespaces(module.preferences.showWhitespace);
    module.textEditor->SetTabSize(module.preferences.tabSize);
    module.initialized = true;

    module.textEditor->SetText("");
    state.editorDirty = false;
}

void shutdown(EditorModule& module) {
    module.textEditor = nullptr;
    gEditor.reset();
}

bool loadDocument(AppState& state, EditorModule& module, const fs::path& path, std::string& error) {
    try {
        const fs::path absolutePath = fs::absolute(path);
        const std::string content = compiler::readFile(absolutePath);
        if (!module.textEditor) {
            error = "Text editor is not initialized.";
            return false;
        }
        module.textEditor->SetText(content);
        state.editorDirty = false;
        state.status = "Loaded: " + absolutePath.string();
        state.collab.lastSyncedContent = content;
        return true;
    } catch (const std::exception& ex) {
        error = std::string("Failed to load file: ") + ex.what();
        return false;
    }
}

bool saveDocument(AppState& state, const EditorModule& module, std::string& error) {
    if (!module.textEditor) {
        error = "Text editor is not initialized.";
        return false;
    }

    if (!state.collab.connected || !state.collab.client) {
        error = "Not connected to collaboration server.";
        return false;
    }

    std::string sendError;
    if (!state.collab.client->send({{"type", "sync"}, {"content", module.textEditor->GetText()}}, sendError)) {
        error = "Failed to send sync: " + sendError;
        return false;
    }

    state.collab.lastSyncedContent = module.textEditor->GetText();
    state.editorDirty = false;
    state.status = "Synchronized with server main.tex.";
    return true;
}

void renderEditor(AppState& state, EditorModule& module, const ImVec2& size) {
    if (!module.textEditor) {
        ImGui::TextUnformatted("Editor unavailable.");
        return;
    }

    auto palette = module.textEditor->GetPalette();
    palette[static_cast<std::size_t>(TextEditor::PaletteIndex::Keyword)] = ImColor(module.preferences.keywordColor);
    palette[static_cast<std::size_t>(TextEditor::PaletteIndex::Default)] = ImColor(module.preferences.textColor);
    palette[static_cast<std::size_t>(TextEditor::PaletteIndex::Background)] = ImColor(module.preferences.backgroundColor);
    palette[static_cast<std::size_t>(TextEditor::PaletteIndex::Comment)] = ImColor(module.preferences.commentColor);
    module.textEditor->SetPalette(palette);

    ImFont* selectedFont = nullptr;
    if (module.activeFontIndex >= 0 && module.activeFontIndex < static_cast<int>(module.loadedFonts.size())) {
        selectedFont = module.loadedFonts[module.activeFontIndex];
    }

    if (selectedFont) {
        ImGui::PushFont(selectedFont);
    }

    module.textEditor->Render("LaTeXSourceEditor", size, false);
    renderAutocomplete(state, module);
    if (module.textEditor->IsTextChanged()) {
        state.editorDirty = true;
        state.lastEditAt = std::chrono::steady_clock::now();
    }

    if (selectedFont) {
        ImGui::PopFont();
    }
}

void applyPreferences(EditorModule& module) {
    if (!module.textEditor) {
        return;
    }

    module.textEditor->SetPalette(makePalette(module.preferences));
    module.textEditor->SetShowWhitespaces(module.preferences.showWhitespace);
    module.textEditor->SetTabSize(module.preferences.tabSize);
}

void ensureFonts(EditorModule& module, ImGuiIO& io) {
    if (module.fontOptions.empty()) {
        module.fontOptions = discoverFonts();
    }

    module.loadedFonts.assign(module.fontOptions.size(), nullptr);
    io.Fonts->Clear();
    module.loadedFonts[0] = io.Fonts->AddFontDefault();

    for (std::size_t i = 1; i < module.fontOptions.size(); ++i) {
        const auto& option = module.fontOptions[i];
        if (!option.path.empty()) {
            module.loadedFonts[i] = io.Fonts->AddFontFromFileTTF(option.path.string().c_str(), module.preferences.fontSize);
        }
    }

    if (module.preferences.selectedFontIndex >= static_cast<int>(module.loadedFonts.size()) ||
        module.loadedFonts[module.preferences.selectedFontIndex] == nullptr) {
        module.preferences.selectedFontIndex = 0;
    }

    module.activeFontIndex = module.preferences.selectedFontIndex;
}


}  // namespace editor
