"""
BotOS Core — Magic Command Registry
=====================================
File:    magic_commands.py
Layer:   L4 — Development Layer
Author:  Berk Elmalı
License: MIT

Provides a registry for "magic" commands — high-level Python
functions accessible via the '!' prefix in BotShell. Each
command is a decorated function that receives arguments as
a string and returns a result.

Built-in magic commands:
  !sysinfo    — Display system information
  !processes  — List running processes
  !meminfo    — Show memory statistics
  !diskinfo   — Show disk usage
  !env        — Display environment variables
  !pyver      — Show Python version info
  !magic      — List all available magic commands
"""

import os
import sys
import platform
from typing import Callable, Dict, Optional, Any


class MagicRegistry:
    """
    Registry for PyBridge magic commands.

    Commands are registered with a name, handler function,
    and optional help text. The registry supports lookup,
    execution, and enumeration.
    """

    def __init__(self):
        self._commands: Dict[str, dict] = {}
        self._register_builtins()

    def register(self, name: str, handler: Callable,
                 help_text: str = "") -> None:
        """
        Register a magic command.

        Args:
            name:      Command name (without '!' prefix).
            handler:   Callable(args: str) -> Any.
            help_text: Short description for help output.
        """
        self._commands[name] = {
            "handler": handler,
            "help": help_text,
        }

    def has_command(self, name: str) -> bool:
        """Check if a magic command is registered."""
        return name in self._commands

    def execute(self, name: str, args: str = "") -> Any:
        """
        Execute a registered magic command.

        Args:
            name: Command name.
            args: Argument string.

        Returns:
            Whatever the handler returns.

        Raises:
            KeyError: If command is not registered.
        """
        if name not in self._commands:
            raise KeyError(f"Unknown magic command: {name}")
        return self._commands[name]["handler"](args)

    def list_commands(self) -> list:
        """Return sorted list of registered command names."""
        return sorted(self._commands.keys())

    def get_help(self, name: str) -> str:
        """Get help text for a command."""
        if name in self._commands:
            return self._commands[name]["help"]
        return ""

    # ── Built-in Magic Commands ──────────────────────────────

    def _register_builtins(self):
        """Register all built-in magic commands."""
        self.register("sysinfo",   self._cmd_sysinfo,
                      "Display system information")
        self.register("processes", self._cmd_processes,
                      "List running processes")
        self.register("meminfo",   self._cmd_meminfo,
                      "Show memory statistics")
        self.register("diskinfo",  self._cmd_diskinfo,
                      "Show disk usage")
        self.register("env",       self._cmd_env,
                      "Display environment variables")
        self.register("pyver",     self._cmd_pyver,
                      "Show Python version info")
        self.register("magic",     self._cmd_magic,
                      "List all available magic commands")

    @staticmethod
    def _cmd_sysinfo(args: str) -> str:
        """Gather and format system information."""
        info = []
        info.append("╔══════════════════════════════════════════╗")
        info.append("║        BotOS — System Information        ║")
        info.append("╚══════════════════════════════════════════╝")
        info.append(f"  Platform    : {platform.platform()}")
        info.append(f"  Architecture: {platform.machine()}")
        info.append(f"  Processor   : {platform.processor() or 'N/A'}")
        info.append(f"  Hostname    : {platform.node()}")
        info.append(f"  Python      : {platform.python_version()}")
        info.append(f"  OS Release  : {platform.release()}")

        try:
            load = os.getloadavg()
            info.append(f"  Load Avg    : {load[0]:.2f}, {load[1]:.2f}, {load[2]:.2f}")
        except (OSError, AttributeError):
            info.append("  Load Avg    : N/A")

        info.append(f"  PID         : {os.getpid()}")
        info.append(f"  UID         : {os.getuid() if hasattr(os, 'getuid') else 'N/A'}")

        return "\n".join(info)

    @staticmethod
    def _cmd_processes(args: str) -> str:
        """List running processes using /proc (Linux-specific)."""
        lines = []
        lines.append(f"  {'PID':>8}  {'COMMAND'}")
        lines.append(f"  {'---':>8}  {'-------'}")

        proc_path = "/proc"
        if not os.path.isdir(proc_path):
            return "  (Process listing not available on this platform)"

        count = 0
        for entry in sorted(os.listdir(proc_path)):
            if entry.isdigit():
                cmdline_path = os.path.join(proc_path, entry, "cmdline")
                try:
                    with open(cmdline_path, "r") as f:
                        cmdline = f.read().replace("\0", " ").strip()
                    if cmdline:
                        lines.append(f"  {entry:>8}  {cmdline[:60]}")
                        count += 1
                except (PermissionError, FileNotFoundError):
                    pass

            if count >= 50:  # Limit output
                lines.append(f"  ... (truncated, {count}+ processes)")
                break

        return "\n".join(lines)

    @staticmethod
    def _cmd_meminfo(args: str) -> str:
        """Show memory info from /proc/meminfo."""
        meminfo_path = "/proc/meminfo"
        if not os.path.isfile(meminfo_path):
            return "  (Memory info not available on this platform)"

        lines = []
        lines.append("  ── Memory Information ──")

        try:
            with open(meminfo_path, "r") as f:
                for line in f:
                    key, _, value = line.partition(":")
                    key = key.strip()
                    value = value.strip()

                    if key in ("MemTotal", "MemFree", "MemAvailable",
                               "Buffers", "Cached", "SwapTotal", "SwapFree"):
                        lines.append(f"  {key:<16}: {value}")
        except PermissionError:
            return "  (Permission denied reading /proc/meminfo)"

        return "\n".join(lines)

    @staticmethod
    def _cmd_diskinfo(args: str) -> str:
        """Show disk usage for root filesystem."""
        try:
            stat = os.statvfs("/")
            total = stat.f_frsize * stat.f_blocks
            free  = stat.f_frsize * stat.f_bfree
            used  = total - free

            def fmt(b):
                for unit in ("B", "KB", "MB", "GB", "TB"):
                    if b < 1024:
                        return f"{b:.1f} {unit}"
                    b /= 1024
                return f"{b:.1f} PB"

            lines = []
            lines.append("  ── Disk Usage (/) ──")
            lines.append(f"  Total : {fmt(total)}")
            lines.append(f"  Used  : {fmt(used)}")
            lines.append(f"  Free  : {fmt(free)}")
            lines.append(f"  Usage : {used / total * 100:.1f}%")
            return "\n".join(lines)

        except (OSError, AttributeError):
            return "  (Disk info not available on this platform)"

    @staticmethod
    def _cmd_env(args: str) -> str:
        """Display environment variables, optionally filtered."""
        lines = []
        env_filter = args.strip().upper() if args.strip() else None

        for key in sorted(os.environ):
            if env_filter and env_filter not in key.upper():
                continue
            val = os.environ[key]
            if len(val) > 80:
                val = val[:77] + "..."
            lines.append(f"  {key}={val}")

        if not lines:
            return f"  (No environment variables matching '{args.strip()}')"

        return "\n".join(lines)

    @staticmethod
    def _cmd_pyver(args: str) -> str:
        """Show detailed Python version information."""
        lines = []
        lines.append(f"  Python {sys.version}")
        lines.append(f"  Executable : {sys.executable}")
        lines.append(f"  Platform   : {sys.platform}")
        lines.append(f"  Prefix     : {sys.prefix}")
        lines.append(f"  Path:")
        for p in sys.path:
            lines.append(f"    - {p}")
        return "\n".join(lines)

    def _cmd_magic(self, args: str) -> str:
        """List all registered magic commands."""
        lines = []
        lines.append("  ── Available Magic Commands ──")
        lines.append("")

        for name in self.list_commands():
            help_text = self.get_help(name)
            lines.append(f"  !{name:<14} {help_text}")

        lines.append("")
        lines.append("  Use !<command> in BotShell to execute.")
        return "\n".join(lines)
