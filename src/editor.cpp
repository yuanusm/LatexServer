#include "editor.h"

#include "TextEditor.h"
#include "compiler.h"

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

namespace fs = std::filesystem;

namespace editor {
namespace {

std::unique_ptr<TextEditor> gEditor;

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
        "\\displaystyle", "\\text", "\\newline", "\\\[", "\\\]", "\\(", "\\)"
    };

    using PaletteIndex = TextEditor::PaletteIndex;
    language.mTokenRegexStrings.push_back({R"((\\[A-Za-z@]+))", PaletteIndex::Keyword});
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
    palette[TextEditor::PaletteIndex::Keyword] = ImColor(preferences.keywordColor);
    palette[TextEditor::PaletteIndex::Comment] = ImColor(preferences.commentColor);
    palette[TextEditor::PaletteIndex::String] = ImColor(preferences.stringColor);
    palette[TextEditor::PaletteIndex::Background] = ImColor(preferences.backgroundColor);
    palette[TextEditor::PaletteIndex::Default] = ImColor(preferences.textColor);
    palette[TextEditor::PaletteIndex::LineNumber] = ImColor(0.55f, 0.58f, 0.64f, 1.0f);
    palette[TextEditor::PaletteIndex::CurrentLineFill] = ImColor(0.16f, 0.18f, 0.22f, 1.0f);
    palette[TextEditor::PaletteIndex::CurrentLineFillInactive] = ImColor(0.13f, 0.14f, 0.18f, 1.0f);
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
    module.textEditor->SetShowWhitespacesEnabled(module.preferences.showWhitespace);
    module.textEditor->SetTabSize(module.preferences.tabSize);
    module.textEditor->SetShowLineNumbersEnabled(module.preferences.showLineNumbers);
    module.initialized = true;

    if (!state.texPath.empty()) {
        std::string error;
        loadDocument(state, module, state.texPath, error);
        if (!error.empty()) {
            state.status = error;
        }
    }
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
        state.texPath = absolutePath;
        state.editorDirty = false;
        state.status = "Loaded: " + absolutePath.string();
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

    if (state.texPath.empty()) {
        error = "No .tex file is selected.";
        return false;
    }

    const bool written = compiler::writeFile(state.texPath, module.textEditor->GetText(), error);
    if (written) {
        state.editorDirty = false;
        state.status = "Saved: " + state.texPath.string();
    }
    return written;
}

void renderEditor(AppState& state, EditorModule& module, const ImVec2& size) {
    if (!module.textEditor) {
        ImGui::TextUnformatted("Editor unavailable.");
        return;
    }

    ImFont* selectedFont = nullptr;
    if (module.activeFontIndex >= 0 && module.activeFontIndex < static_cast<int>(module.loadedFonts.size())) {
        selectedFont = module.loadedFonts[module.activeFontIndex];
    }

    if (selectedFont) {
        ImGui::PushFont(selectedFont);
    }

    const bool changed = module.textEditor->Render("LaTeXSourceEditor", size, false);
    if (changed) {
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
    module.textEditor->SetShowWhitespacesEnabled(module.preferences.showWhitespace);
    module.textEditor->SetTabSize(module.preferences.tabSize);
    module.textEditor->SetShowLineNumbersEnabled(module.preferences.showLineNumbers);
    module.fontAtlasDirty = true;
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
    module.fontAtlasDirty = false;
}


}  // namespace editor
