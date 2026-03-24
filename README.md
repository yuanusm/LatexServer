# LatexServer with Dear ImGui

A local LaTeX editor for GLFW/OpenGL + Dear ImGui that uses `ImGuiColorTextEdit` for the main editing surface and `latexmk` for compilation. The application focuses on editing, saving, and compiling `.tex` files instead of trying to render PDFs inside the UI.

## Features

- Full-window `ImGuiColorTextEdit` editor with syntax highlighting, line numbers, and efficient large-file editing.
- Custom LaTeX language definition with command, comment, and math highlighting.
- Top toolbar with `Open`, `Save`, `Compile`, `Auto`, `Preferences`, and collaboration connect/disconnect controls.
- Save-before-compile workflow for both manual and auto compilation.
- Floating preferences window for font, palette, line-number, whitespace, and tab-size settings.
- `latexmk` compilation with absolute paths and Windows-safe double-quoted path handling.


## Collaboration (authoritative server)

This project now includes a minimal TCP client/server collaboration layer using standalone ASIO and `nlohmann/json`.

- `latex_collab_server` is the authoritative server for document content, file operations, and connected users.
- `latex_server` can connect to the server from the toolbar (`Host`, `Port`, `Connect`, `Disconnect`).
- Document sync uses an overwrite model (full text `sync` messages) with a 400 ms client debounce.
- Message framing is fixed-length prefix + JSON payload: `[uint32 length][JSON payload]`.
- File messages are prepared for future extensions (CRDT/auth placeholders are protocol-level only; not implemented).

### Protocol messages

Implemented now:

- `sync`
- `file_list`, `file_open`, `file_save`, `file_create`, `file_delete`, `file_rename`, `file_upload`
- `users`

Reserved for later (not implemented):

- `auth`
- `op`

### Run collaboration server

```bash
./build/latex_collab_server 9090
```

## Dependencies

- CMake 3.16+
- OpenGL
- GLFW 3.3+
- A LaTeX distribution that provides `latexmk` (MiKTeX works)
- Network access during configure time so CMake can fetch Dear ImGui and ImGuiColorTextEdit

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Run

```bash
./build/latex_server
```

On startup the application resolves the project root, changes the working directory there, and loads `assets/sample.tex` into the editor.

## UI Layout

- **Top:** toolbar with file, compile, and preferences controls.
- **Center:** full-size LaTeX text editor.
- **Floating window:** preferences panel.

## Compilation command

The compiler builds and logs a command like this:

```text
latexmk -pdf -interaction=nonstopmode -synctex=1 -outdir="C:/path/to/project/build-output" "C:/path/to/project/assets/sample.tex"
```

Compilation logs are redirected to `build-output/latexmk.log`.
