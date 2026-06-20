# BotOS Core — System Architecture

> Comprehensive architectural reference for the BotOS Core Platform.

---

## Layer Model

BotOS follows a strict 6-layer abstraction hierarchy. Each layer communicates
only with its immediate neighbors, enforcing clean separation of concerns.

```
┌─────────────────────────────────────────────────────────────┐
│                                                             │
│   L6 ── User Interface Layer                                │
│          BotDesk (Window Manager)                           │
│          BotPaint (Graphics Application)                    │
│          Editor (Text Editor)                               │
│                         │                                   │
├─────────────────────────┼───────────────────────────────────┤
│                         ▼                                   │
│   L5 ── Platform Services                                   │
│          BotPkg  (Package Manager & Dependency Resolver)    │
│          BotNet  (HTTP/TCP Network Stack)                   │
│          BotLog  (Async Ring-Buffer Logger)                 │
│                         │                                   │
├─────────────────────────┼───────────────────────────────────┤
│                         ▼                                   │
│   L4 ── Development Layer                                   │
│          BotShell  (POSIX Shell + PyBridge Engine)          │
│          BotSDK    ┬─ libbot (Core Utilities)               │
│                    └─ BotUI  (X11/SDL2 GUI Abstraction)     │
│          Python Runtime (PyBridge Bridge)                   │
│                         │                                   │
├─────────────────────────┼───────────────────────────────────┤
│                         ▼                                   │
│   L3 ── Abstraction Layer                                   │
│          VFS  (Virtual File System)                         │
│               ┬─ Ext4 Driver                                │
│               ├─ RAMfs Driver                               │
│               └─ NetFS Driver (via BotNet)                  │
│          IPC  (Inter-Process Communication)                 │
│               ┬─ Shared Memory                              │
│               ├─ Message Queues                             │
│               └─ Signal Handlers                            │
│                         │                                   │
├─────────────────────────┼───────────────────────────────────┤
│                         ▼                                   │
│   L2 ── Core Kernel                                         │
│          Linux Kernel (LTS)                                 │
│          Init Daemon (PID 1)                                │
│          GRUB Bootloader (Custom Themed)                    │
│                         │                                   │
├─────────────────────────┼───────────────────────────────────┤
│                         ▼                                   │
│   L1 ── Hardware / Virtual Machine                          │
│          x86_64 CPU                                         │
│          VirtualBox / QEMU Hypervisor                       │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## Component Specifications

### VFS (Virtual File System) — `core/vfs/`

The VFS provides hardware-independent file operations through a driver-based
architecture using C function pointers.

**Key Structures:**
```c
struct vfs_driver {
    const char *name;
    int (*open)(const char *path, int flags);
    ssize_t (*read)(int fd, void *buf, size_t count);
    ssize_t (*write)(int fd, const void *buf, size_t count);
    int (*close)(int fd);
    int (*stat)(const char *path, struct bot_stat *st);
};
```

**Mount Table:** Routes paths to their corresponding driver based on mount
point prefixes (e.g., `/ram/` → RAMfs, `/net/` → NetFS).

---

### BotShell — `shell/`

POSIX-compatible interactive shell with PyBridge extension.

**Execution Flow:**
```
Input → Tokenizer → Parser → Executor
                        │
                        ├─ Built-in? → Execute directly
                        ├─ External? → fork() + execvp()
                        └─ PyBridge? (! prefix) → Python bridge
```

---

### BotSDK — `sdk/`

Two sub-libraries:
- **libbot**: Core utilities (I/O wrappers, string ops, memory pools)
- **BotUI**: X11/SDL2 GUI abstraction (windows, canvas, event loop)

---

### Platform Services — `services/`

| Service | Purpose |
|---------|---------|
| BotPkg  | `.botpkg` archive management, manifest parsing, dependency DAG resolution |
| BotNet  | TCP socket layer, HTTP client for package fetching and network I/O |
| BotLog  | Async ring-buffer logger with severity levels and file rotation |

---

## Build Pipeline

```
Source Code (C/Python)
        │
        ▼
  CMake Configure ──→ compile_commands.json
        │
        ▼
  GCC/Clang Compile ──→ .o object files
        │
        ▼
  Link Libraries ──→ libbot.a, libvfs.a, libbotui.a, ...
        │
        ▼
  Link Executables ──→ botshell, botpkg, botdesk, ...
        │
        ▼
  Buildroot Integration ──→ rootfs.ext4 + bzImage
        │
        ▼
  QEMU / VirtualBox ──→ Running BotOS Instance
```
