# LatexServer with Dear ImGui

A minimal local LaTeX editor for Windows-oriented GLFW/OpenGL setups. The app now embeds Dear ImGui for an editor-style interface, compiles with `latexmk`, writes output into `build-output`, and opens generated PDFs with the default Windows viewer.

## Features

- Dear ImGui editor UI with a split layout.
- `latexmk -pdf -interaction=nonstopmode -synctex=1 -outdir=...` compilation flow.
- Absolute path handling for source, output, and log files.
- Windows-safe command building with double quotes only.
- Optional auto-compile mode with background compilation.
- PDF preview placeholder panel and an `Open PDF` button.

## Dependencies

- CMake 3.16+
- OpenGL
- GLFW 3.3+
- A LaTeX distribution that provides `latexmk` (MiKTeX works)
- Network access during configure time so CMake can fetch Dear ImGui

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Run

```bash
./build/latex_server
```

On startup the application resolves the project root, changes the working directory there, and loads `assets/sample.tex` into the left editor pane.

## UI

- **Left panel:** editable LaTeX source via `InputTextMultiline`.
- **Right panel:** placeholder PDF preview panel.
- **Bottom bar:** `Compile`, `Auto`, and `Open PDF` controls.

## Compilation command

The compile worker builds and logs a command like this:

```text
latexmk -pdf -interaction=nonstopmode -synctex=1 -outdir="C:/path/to/project/build-output" "C:/path/to/project/assets/sample.tex"
```

Compilation logs are redirected to `build-output/latexmk.log`.
