# BotOS Core — Development Roadmap

> Master execution roadmap tracking all development phases.

---

## Phase Overview

| # | Milestone | Description | Status | Target |
|---|-----------|-------------|--------|--------|
| 1 | **Core Boot & Architecture** | System boots in VirtualBox; layer architecture defined | ✅ Complete | — |
| 2 | **BotShell v1** | Basic command engine; C-based core structure | ✅ Complete | — |
| 3 | **VFS & Core Shell Integration** | `bot_vfs.h` integration; filesystem abstraction; first API set | 🔄 Active | v0.3.0 |
| 4 | **PyBridge Subsystem** | Python runtime integration; script bridge; magic commands | 📋 Planned | v0.4.0 |
| 5 | **BotSDK & BotPkg** | UI libraries; dependency management; network package distribution | 📋 Planned | v0.5.0 |
| 6 | **BotDesk GUI & App Ecosystem** | X11/SDL2 window manager; BotPaint and Editor core apps | 📋 Planned | v0.6.0 |

---

## Phase 3 — VFS & Core Shell Integration (Active)

### Goals
- [ ] Implement `struct vfs_driver` with function pointer interface
- [ ] Create Ext4 driver stub with real `open()`/`read()`/`write()` passthrough
- [ ] Create RAMfs driver with in-memory file storage
- [ ] Create NetFS driver stub (bridging to BotNet)
- [ ] Implement mount table and path-based driver dispatch
- [ ] Integrate VFS into BotShell (`cat`, `ls`, `read` commands use VFS)
- [ ] Write unit tests for VFS operations
- [ ] Update BotSDK's `bot_io` to use VFS backend

### Deliverables
- `core/vfs/` — Complete VFS implementation
- `core/ipc/` — Basic IPC primitives
- `core/init/` — Init daemon skeleton
- Shell integration with VFS-backed file commands

---

## Phase 4 — PyBridge Subsystem (Planned)

### Goals
- [ ] Embed Python 3 interpreter via `Py_Initialize()` / `Py_Finalize()`
- [ ] Implement C→Python command bridge (stdin/stdout protocol)
- [ ] Create magic command registry in Python
- [ ] Support `!command` syntax in BotShell for PyBridge dispatch
- [ ] Support `.bot` script file execution
- [ ] Add system introspection commands (process list, memory stats)

---

## Phase 5 — BotSDK & BotPkg (Planned)

### Goals
- [ ] Implement `libbot` utility library (I/O, strings, memory)
- [ ] Create BotUI window management with X11/SDL2
- [ ] Implement canvas drawing primitives
- [ ] Build async event loop for keyboard/mouse input
- [ ] Design `.botpkg` archive format
- [ ] Implement `manifest.json` parser
- [ ] Build dependency DAG resolver
- [ ] Create `botpkg install/remove/update` CLI

---

## Phase 6 — BotDesk GUI & App Ecosystem (Planned)

### Goals
- [ ] Build minimal X11 window manager (BotDesk)
- [ ] Implement window stacking, focus, and movement
- [ ] Create BotPaint with basic drawing tools
- [ ] Create Editor with syntax highlighting
- [ ] Desktop launcher and application menu
- [ ] Theme system (colors, fonts, icons)
