#!/usr/bin/env python3
"""
BotOS Core — Scaffold Validation Script
========================================
Performs static analysis on the BotOS source tree to verify:
  1. All #include'd headers exist in the project
  2. Every function declared in .h files has a stub in .c files
  3. Every source file referenced in CMakeLists.txt exists
  4. CMake target dependencies are consistent
  5. Include guard conventions are followed

Usage:
    python scripts/validate_scaffold.py
"""

import os
import re
import sys
import io
from pathlib import Path
from typing import Dict, List, Set, Tuple

# Force UTF-8 output on Windows consoles
try:
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding="utf-8", errors="replace")
except Exception:
    pass

# ── Configuration ────────────────────────────────────────────

PROJECT_ROOT = Path(__file__).resolve().parent.parent
TOTAL_CHECKS = 0
TOTAL_PASSED = 0
TOTAL_WARNINGS = 0
TOTAL_ERRORS = 0

# ANSI Colors
GREEN  = "\033[32m"
RED    = "\033[31m"
YELLOW = "\033[33m"
CYAN   = "\033[36m"
BOLD   = "\033[1m"
DIM    = "\033[2m"
RESET  = "\033[0m"

# ── Helpers ──────────────────────────────────────────────────

def log_pass(msg: str):
    global TOTAL_PASSED, TOTAL_CHECKS
    TOTAL_CHECKS += 1
    TOTAL_PASSED += 1
    print(f"  {GREEN}[OK]{RESET} {msg}")

def log_fail(msg: str):
    global TOTAL_ERRORS, TOTAL_CHECKS
    TOTAL_CHECKS += 1
    TOTAL_ERRORS += 1
    print(f"  {RED}[FAIL]{RESET} {msg}")

def log_warn(msg: str):
    global TOTAL_WARNINGS, TOTAL_CHECKS
    TOTAL_CHECKS += 1
    TOTAL_WARNINGS += 1
    print(f"  {YELLOW}[WARN]{RESET} {msg}")

def log_section(title: str):
    print(f"\n  {BOLD}{CYAN}── {title} ──{RESET}\n")


def find_files(root: Path, ext: str) -> List[Path]:
    """Recursively find all files with the given extension."""
    return sorted(root.rglob(f"*{ext}"))


def read_file(path: Path) -> str:
    """Read file contents, handling encoding gracefully."""
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except Exception:
        return ""


# ── Check 1: Include Consistency ─────────────────────────────

def check_includes():
    """Verify all #include "..." headers can be resolved within the project."""
    log_section("Include Consistency")

    # Build index of all .h files by basename
    all_headers: Dict[str, List[Path]] = {}
    for h in find_files(PROJECT_ROOT, ".h"):
        name = h.name
        all_headers.setdefault(name, []).append(h)

    # System headers we expect from POSIX/stdlib (skip these)
    system_headers = {
        "stdio.h", "stdlib.h", "string.h", "stddef.h", "stdint.h",
        "stdarg.h", "errno.h", "signal.h", "unistd.h", "ctype.h",
        "fcntl.h", "time.h", "assert.h",
        "sys/wait.h", "sys/types.h", "sys/socket.h", "sys/time.h",
        "netinet/in.h", "arpa/inet.h", "netdb.h",
        "Python.h",
    }

    c_files = find_files(PROJECT_ROOT, ".c") + find_files(PROJECT_ROOT, ".h")
    include_re = re.compile(r'#include\s+"([^"]+)"')

    for src in c_files:
        content = read_file(src)
        for match in include_re.finditer(content):
            inc = match.group(1)
            basename = os.path.basename(inc)

            if inc in system_headers or basename in system_headers:
                continue

            if basename in all_headers:
                log_pass(f"{src.relative_to(PROJECT_ROOT)} -> {inc}")
            else:
                log_fail(f"{src.relative_to(PROJECT_ROOT)} -> {inc} {RED}NOT FOUND{RESET}")


# ── Check 2: Function Prototype Coverage ─────────────────────

def check_prototypes():
    """Verify every function declared in .h files has a definition in .c files."""
    log_section("Function Prototype Coverage")

    # Regex for function declarations in headers (simplified)
    proto_re = re.compile(
        r'^(?:(?:static|extern|inline)\s+)*'
        r'(?:const\s+)?'
        r'(?:int|void|char|ssize_t|size_t|int64_t|pid_t|uint32_t|'
        r'bot_\w+|parsed_cmd_t|pipeline_t|exec_result_t|resolve_result_t|'
        r'bot_pool_t|bot_window_t|bot_canvas_t|magic_cmd_handler_t)\s*\*?\s+'
        r'(\w+)\s*\(',
        re.MULTILINE
    )

    # Build index of all function definitions in .c files
    c_files = find_files(PROJECT_ROOT, ".c")
    defined_funcs: Set[str] = set()

    def_re = re.compile(
        r'^(?:static\s+)?'
        r'(?:const\s+)?'
        r'(?:unsigned\s+|signed\s+|struct\s+)?'
        r'\w[\w\s\*]*\s+'
        r'(\w+)\s*\([^;]*?\)\s*\{',
        re.MULTILINE
    )

    # Also scan for function names directly (handles edge cases)
    simple_def_re = re.compile(r'^(\w+)\s*\(', re.MULTILINE)

    for cf in c_files:
        content = read_file(cf)
        for match in def_re.finditer(content):
            defined_funcs.add(match.group(1))
        for match in simple_def_re.finditer(content):
            name = match.group(1)
            if name not in ("if", "for", "while", "switch", "return", "typedef", "struct", "enum"):
                defined_funcs.add(name)

    # Check each header's prototypes
    h_files = find_files(PROJECT_ROOT, ".h")
    skip_funcs = {"main"}  # main() won't be in headers

    for hf in h_files:
        content = read_file(hf)
        rel = hf.relative_to(PROJECT_ROOT)

        for match in proto_re.finditer(content):
            func_name = match.group(1)
            if func_name in skip_funcs:
                continue

            if func_name in defined_funcs:
                log_pass(f"{rel}: {func_name}()")
            else:
                log_warn(f"{rel}: {func_name}() -- {YELLOW}no definition found{RESET}")


# ── Check 3: CMake Source File Verification ──────────────────

def check_cmake_sources():
    """Verify every source file referenced in CMakeLists.txt actually exists."""
    log_section("CMake Source File References")

    cmake_files = find_files(PROJECT_ROOT, "CMakeLists.txt")

    # Match add_library/add_executable source file references
    src_re = re.compile(r'(?:add_library|add_executable)\s*\(\s*\w+(?:\s+\w+)?\s+([\s\S]*?)\)', re.MULTILINE)
    file_re = re.compile(r'((?:src|include)/[\w/.]+\.[ch])')

    for cmake in cmake_files:
        content = read_file(cmake)
        cmake_dir = cmake.parent
        rel_cmake = cmake.relative_to(PROJECT_ROOT)

        for block_match in src_re.finditer(content):
            block = block_match.group(1)
            for file_match in file_re.finditer(block):
                src_file = file_match.group(1)
                full_path = cmake_dir / src_file

                if full_path.exists():
                    log_pass(f"{rel_cmake} -> {src_file}")
                else:
                    log_fail(f"{rel_cmake} -> {src_file} {RED}MISSING{RESET}")


# ── Check 4: Include Guards ──────────────────────────────────

def check_include_guards():
    """Verify all .h files have proper #ifndef include guards."""
    log_section("Include Guards")

    guard_re = re.compile(r'#ifndef\s+(\w+)\s*\n#define\s+(\w+)')

    for hf in find_files(PROJECT_ROOT, ".h"):
        content = read_file(hf)
        rel = hf.relative_to(PROJECT_ROOT)

        match = guard_re.search(content)
        if match:
            g1, g2 = match.group(1), match.group(2)
            if g1 == g2:
                log_pass(f"{rel} -- {g1}")
            else:
                log_fail(f"{rel} -- guard mismatch: {g1} vs {g2}")
        else:
            log_warn(f"{rel} -- {YELLOW}no include guard found{RESET}")


# ── Check 5: CMake Subdirectory Consistency ──────────────────

def check_cmake_subdirs():
    """Verify every add_subdirectory() target directory has a CMakeLists.txt."""
    log_section("CMake Subdirectory Targets")

    subdir_re = re.compile(r'add_subdirectory\s*\(\s*(\w+)\s*\)')

    for cmake in find_files(PROJECT_ROOT, "CMakeLists.txt"):
        content = read_file(cmake)
        cmake_dir = cmake.parent
        rel_cmake = cmake.relative_to(PROJECT_ROOT)

        for match in subdir_re.finditer(content):
            subdir = match.group(1)
            child_cmake = cmake_dir / subdir / "CMakeLists.txt"

            if child_cmake.exists():
                log_pass(f"{rel_cmake} -> {subdir}/CMakeLists.txt")
            else:
                log_fail(f"{rel_cmake} -> {subdir}/CMakeLists.txt {RED}MISSING{RESET}")


# ── Check 6: File Statistics ─────────────────────────────────

def print_stats():
    """Print file count statistics."""
    log_section("Project Statistics")

    c_files  = find_files(PROJECT_ROOT, ".c")
    h_files  = find_files(PROJECT_ROOT, ".h")
    py_files = find_files(PROJECT_ROOT, ".py")
    cm_files = find_files(PROJECT_ROOT, "CMakeLists.txt")
    sh_files = find_files(PROJECT_ROOT, ".sh")

    total_c_lines = sum(len(read_file(f).splitlines()) for f in c_files)
    total_h_lines = sum(len(read_file(f).splitlines()) for f in h_files)
    total_py_lines = sum(len(read_file(f).splitlines()) for f in py_files if f.name != "validate_scaffold.py")

    print(f"  {'C source files:':<30} {len(c_files)}")
    print(f"  {'C header files:':<30} {len(h_files)}")
    print(f"  {'Python files:':<30} {len(py_files) - 1}")  # exclude this script
    print(f"  {'CMake files:':<30} {len(cm_files)}")
    print(f"  {'Shell scripts:':<30} {len(sh_files)}")
    print(f"  {'C lines (source):':<30} {total_c_lines}")
    print(f"  {'C lines (headers):':<30} {total_h_lines}")
    print(f"  {'Python lines:':<30} {total_py_lines}")
    print(f"  {'Total C/H lines:':<30} {total_c_lines + total_h_lines}")


# ── Main ─────────────────────────────────────────────────────

def main():
    print(f"\n  {BOLD}{CYAN}+============================================+{RESET}")
    print(f"  {BOLD}{CYAN}|    BotOS Scaffold Validation Suite         |{RESET}")
    print(f"  {BOLD}{CYAN}+============================================+{RESET}")

    check_includes()
    check_prototypes()
    check_cmake_sources()
    check_include_guards()
    check_cmake_subdirs()
    print_stats()

    print(f"\n  {BOLD}{'=' * 46}{RESET}")
    print(f"  {BOLD}Results:{RESET}")
    print(f"    {GREEN}Passed:   {TOTAL_PASSED}{RESET}")
    print(f"    {YELLOW}Warnings: {TOTAL_WARNINGS}{RESET}")
    print(f"    {RED}Errors:   {TOTAL_ERRORS}{RESET}")
    print(f"    Total:    {TOTAL_CHECKS}")
    print()

    if TOTAL_ERRORS > 0:
        print(f"  {RED}{BOLD}BUILD VALIDATION FAILED{RESET} -- {TOTAL_ERRORS} error(s) found\n")
        return 1
    elif TOTAL_WARNINGS > 0:
        print(f"  {YELLOW}{BOLD}BUILD VALIDATION PASSED WITH WARNINGS{RESET}\n")
        return 0
    else:
        print(f"  {GREEN}{BOLD}BUILD VALIDATION PASSED{RESET} -- all checks green!\n")
        return 0


if __name__ == "__main__":
    sys.exit(main())
