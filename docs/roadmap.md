# BotOS Core — Development Roadmap

> Master execution roadmap tracking all development phases.

---

# BotOS Core — Development Roadmap

> Master execution roadmap tracking all development phases.
>
> **2026-07-11 update:** this file was badly out of date — phases 4–6
> were marked "Planned" with unchecked goals even though the code for
> all of them existed and, once actually build-tested (see
> `durum1.md`), largely worked. Statuses below now reflect what was
> independently verified by compiling every target and running the
> test suite, not just what the source tree contains.

---

## Phase Overview

| # | Milestone | Description | Status | Target |
|---|-----------|-------------|--------|--------|
| 1 | **Core Boot & Architecture** | System boots in VirtualBox; layer architecture defined | ⚠️ Userspace verified, boot not re-tested | — |
| 2 | **BotShell v1** | Basic command engine; C-based core structure | ✅ Complete & verified | — |
| 3 | **VFS & Core Shell Integration** | `bot_vfs.h` integration; filesystem abstraction; first API set | ✅ Complete & verified | v0.3.0 |
| 4 | **PyBridge Subsystem** | Python runtime integration; script bridge; magic commands | ✅ Complete & verified | v0.4.0 |
| 5 | **BotSDK & BotPkg** | UI libraries; dependency management; network package distribution | ✅ Complete (BotPkg's network layer unverified, see note) | v0.5.0 |
| 6 | **BotDesk GUI & App Ecosystem** | X11/SDL2 window manager; BotPaint and Editor core apps | ✅ Complete — real multi-window management (2026-07-12) | v0.6.0 |

---

## Phase 3 — VFS & Core Shell Integration (Complete)

### Goals
- [x] Implement `struct vfs_driver` with function pointer interface
- [x] Create Ext4 driver with real `open()`/`read()`/`write()` passthrough
- [x] Create RAMfs driver with in-memory file storage
- [x] Create NetFS driver stub (bridging to BotNet)
- [x] Implement mount table and path-based driver dispatch
- [x] Integrate VFS into BotShell (`cat`, `ls`, `read` commands use VFS)
- [x] Write unit tests for VFS operations (`tests/test_vfs.c`, 5/5 passing)
- [x] Update BotSDK's `bot_io` to use VFS backend

### Deliverables
- `core/vfs/` — Complete VFS implementation, fine-grained mutex locking confirmed correct on read-through
- `core/ipc/` — Message queues + real POSIX `shm_open`/`mmap` shared memory, confirmed working via `tests/test_ipc.c` (7/7 passing, including a cross-process SHM test)
- `core/init/` — Init daemon
- Shell integration with VFS-backed file commands

---

## Phase 4 — PyBridge Subsystem (Complete)

### Goals
- [x] Embed Python 3 interpreter via `Py_Initialize()` / `Py_Finalize()`
- [x] Implement C→Python command bridge (stdin/stdout protocol)
- [x] Create magic command registry in Python
- [x] Support `!command` syntax in BotShell for PyBridge dispatch
- [x] Support `.bot` script file execution
- [x] Add system introspection commands (process list, memory stats)

Builds and links cleanly against a real `Python.h`/`libpython3`. The
`runtime` package that ships under `pybridge/runtime/` isn't on
`PYTHONPATH` by default in this sandbox (shows as a `ModuleNotFoundError`
warning at startup) — worth double-checking the intended install
location for that package against `PYTHONPATH`/`sys.path` setup on a
real target system.

---

## Phase 5 — BotSDK & BotPkg (Complete, BotPkg network layer unverified)

### Goals
- [x] Implement `libbot` utility library (I/O, strings, memory)
- [x] Create BotUI window management with X11/SDL2
- [x] Implement canvas drawing primitives
- [x] Build async event loop for keyboard/mouse input
- [x] Design `.botpkg` archive format
- [x] Implement `manifest.json` parser (bounds-checked, reviewed line by line — no issues found)
- [x] Build dependency DAG resolver
- [x] Create `botpkg install/remove/update` CLI

`services/botnet` (and therefore `botpkg`, which links against it)
depends on `OpenSSL` (`find_package(OpenSSL REQUIRED)`), and its HTTPS
client in `http.c` correctly sets `SSL_VERIFY_PEER` and SNI/hostname
verification on manual read-through. None of this could actually be
*compiled* in the sandbox this audit ran in (no `libssl-dev` present,
no network access to install it), so treat the network layer as
reviewed-but-not-build-verified until it's been compiled somewhere
with OpenSSL's dev headers available.

**2026-07-21:** `botpkg install` had two real bugs, both found by
reading the code and then confirmed with a test harness (a local
stand-in for the HTTP layer, so this didn't need OpenSSL either): a
failed archive download was registered as a successful install
anyway, and the package's manifest was never actually fetched (only
ever read from a cache nothing populated), so version/description
info was always a placeholder and no checksum could ever be checked.
Both fixed, and archives are now verified against a `sha256` field in
the manifest via a small dependency-free SHA-256 implementation
(`services/botpkg/src/sha256.c`) — see `durum2.md`'s second addendum
for the full test transcript. `botpkg_install` still doesn't unpack a
verified archive anywhere after downloading it, which is its own,
larger gap not addressed here.

---

## Phase 6 — BotDesk GUI & App Ecosystem (Complete)

### Goals
- [x] Build minimal X11 window manager (BotDesk)
- [x] Implement window stacking, focus, and movement
- [x] Create BotPaint with basic drawing tools
- [x] Create Editor with syntax highlighting
- [x] Desktop launcher and application menu
- [x] Theme system (colors, fonts, icons)

`sdk/botui`'s Xlib backend (`window.c`/`event.c`) is fully implemented
but was never actually reachable: nothing in the CMake build ever
defined the `BOTOS_HAS_X11` macro it's gated behind, so every app
silently built against the headless `/dev/fb0` framebuffer fallback
instead, and a real cross-file linkage bug (`g_display` /
`g_wm_delete_message` declared `static` in `window.c` but referenced
`extern` from `event.c`) meant it wouldn't even have linked if someone
had force-enabled it. Both are now fixed — `sdk/botui/CMakeLists.txt`
detects X11 the same way it already detected SDL2, and `botdesk` has
been confirmed (via a real Xvfb X server in this sandbox) to open an
actual 1024×768 window.

**2026-07-12:** "window stacking, focus, and movement" above was
checked off for having the *code* to do those things, but BotDesk
didn't actually use any of it — launching an app destroyed BotDesk's
own window, ran the app fullscreen, and blocked until it exited, so
only one thing was ever on screen at a time. BotDesk now acts as a
real (if minimal) reparenting window manager when X11 is available:
decorated, draggable, closable windows, multiple apps running and
visible at once, and a taskbar that lists real open windows. See the
"Gerçek Çoklu Pencere Desteği" addendum in `durum2.md` for what was
tested and how.

**2026-07-14:** edge-drag resize (grip in the bottom-right corner, a
real grabbable margin around each frame) and Alt+Tab/Alt+Shift+Tab
window cycling added. Still not done: virtual desktops/tiling,
minimize.

**2026-07-23:** three more additions, all covered in durum2.md's
third addendum: a Ctrl+Space command palette (Spotlight-style quick
launcher), Ctrl+Shift+S screenshot capture (self-contained BMP
writer, no new dependency), and a cross-process toast notification
system (`services/botnotify`) any program can use via a small FIFO
protocol — built that way after discovering `bot_ipc`'s `bot_mq_*`
message queues are process-local only despite the name, so they
couldn't have served this.

Known gap: BotShell's `if`/`for`/`while` block parser doesn't track
nesting, so a control-flow block nested inside another one closes the
outer block early (the inner `fi`/`done` is read as closing whichever
block started first). Flat blocks work correctly and are covered by
`tests/test_shell.c`; nested ones are a parser limitation to revisit,
not a crash or memory-safety issue.

