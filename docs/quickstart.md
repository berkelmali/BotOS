# 🚀 BotOS Core — Developer Quickstart Guide

This guide provides step-by-step instructions for setting up the development environment, compiling BotOS Core, running unit tests, and executing local VM emulation.

---

## 📋 Prerequisites

Ensure the following tools are installed on your system:

| Tool | Recommended Version | Purpose |
| :--- | :--- | :--- |
| **GCC / Clang** | C11 compliant (`gcc >= 10` or `clang >= 12`) | Core C Compiler |
| **CMake** | `>= 3.20` | Build System Generator |
| **Make / Ninja** | Latest | Build Automator |
| **Python** | `>= 3.10` | PyBridge Runtime & Scaffold Validator |
| **QEMU** | `qemu-system-x86_64` | Hardware Emulation (Optional) |

---

## 🛠️ Building BotOS Core

### 1. Clone the Repository
```bash
git clone https://github.com/berkelmali/BotOS.git
cd BotOS
```

### 2. Configure & Build with CMake
```bash
# Configure build output directory
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBOTOS_BUILD_TESTS=ON -DBOTOS_BUILD_APPS=ON

# Compile all targets
cmake --build build --config Release
```

### 3. Run Unit Tests
```bash
cd build
ctest --output-on-failure
```

---

## 🧪 Helper Scripts

BotOS includes convenient shell scripts in the `scripts/` directory:

```bash
# Build full project
./scripts/build.sh

# Run scaffold structure validation
python3 scripts/validate_scaffold.py

# Launch QEMU Emulation
./scripts/run_qemu.sh

# Clean build artifacts
./scripts/clean.sh
```

---

## 🏗️ Architecture Overview

For details on the 6-layer architecture and component interaction graphs, refer to [Architecture Documentation](architecture.md) and the main [README](../README.md).
