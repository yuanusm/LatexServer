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
- No separate OpenGL loader package is required; the Dear ImGui OpenGL3 backend loader is used directly

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

## Troubleshooting

If you still see an error that mentions `build/_deps/glad-src/CMakeLists.txt`, your local `build/` directory was generated from an older revision. The current git version no longer uses `glad`, so remove the old build tree and configure again:

```bash
rm -rf build
cmake -S . -B build
cmake --build build
```

On Windows/MSYS2 MinGW64 you can also delete the `build` folder from Explorer and rerun the same commands from a fresh shell.

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
