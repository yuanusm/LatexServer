# Local LaTeX Editor

A minimal desktop LaTeX editor built with Dear ImGui, GLFW, OpenGL, and MuPDF. It behaves like a local-only lightweight Overleaf: edit `main.tex` on the left, compile with `latexmk`, and preview the generated PDF on the right.

## Features

- Split UI for editing and first-page PDF preview.
- Manual and auto-compile modes using `latexmk -pdf -interaction=nonstopmode -outdir=build main.tex`.
- Timestamp-based file watching for both `main.tex` and `build/main.pdf`.
- MuPDF-powered first-page rendering into an OpenGL texture.
- Generated `build/` and `tikz_cache/` directories are preserved.
- `DocumentState` struct included for future collaborative/networked sync support.

## Build requirements

- CMake 3.20+
- A C++17 compiler (MinGW on Windows is the primary target)
- OpenGL
- GLFW 3
- MuPDF development package
- `latexmk` available on `PATH`
- Dear ImGui sources, either:
  - supplied through `-DIMGUI_DIR=/path/to/imgui`, or
  - fetched automatically by CMake (`LATEX_EDITOR_FETCH_IMGUI=ON`, default)

## Build

```bash
cmake -S . -B build-app -G "MinGW Makefiles"
cmake --build build-app -j
```

If GLFW or MuPDF are installed in custom prefixes, point CMake at them in the usual way (for example via `CMAKE_PREFIX_PATH` or `PKG_CONFIG_PATH`).

## Run

```bash
./build-app/local_latex_editor
```

On startup the app ensures `build/` and `tikz_cache/` exist, compiles `main.tex`, and displays the first page of `build/main.pdf` when compilation succeeds.

## Notes

- Auto-compile saves the editor buffer first, then recompiles only when `main.tex` timestamp changes.
- PDF textures are only regenerated when `build/main.pdf` changes.
- Auxiliary LaTeX files are intentionally preserved inside `build/` for faster incremental builds and debugging.
