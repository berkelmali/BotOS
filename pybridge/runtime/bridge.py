"""
BotOS Core — PyBridge Python Runtime
================================
File:    bridge.py
Layer:   L4 — Development Layer
Author:  Berk Elmalı
License: MIT

Central dispatch module for the PyBridge engine. Receives
command strings from the C bridge (pybridge.c), resolves
them against the magic command registry, and executes them.

The bridge supports three execution modes:
  1. Magic commands  — Registered Python handlers (e.g., !sysinfo)
  2. Python eval     — Direct Python expression evaluation
  3. Python exec     — Multi-line Python code execution
"""

import sys
import os
import traceback
from io import StringIO
from typing import Any, Optional

from .magic_commands import MagicRegistry


class PyBridge:
    """
    Main bridge class coordinating command dispatch between
    C (BotShell) and Python (magic commands / eval / exec).
    """

    def __init__(self):
        self.registry = MagicRegistry()
        self.context: dict = {}  # Persistent execution context
        self._setup_default_context()

    def _setup_default_context(self):
        """Set up default variables available in PyBridge context."""
        self.context.update({
            "__name__": "__pybridge__",
            "__doc__": "BotOS PyBridge execution context",
            "os": os,
            "sys": sys,
        })

    def execute(self, command: str) -> int:
        """
        Execute a PyBridge command string.

        Dispatch order:
          1. Check if it's a registered magic command
          2. Try Python eval (expression)
          3. Fall back to Python exec (statement)

        Args:
            command: The command string (! prefix already stripped).

        Returns:
            0 on success, -1 on error.
        """
        if not command or not command.strip():
            return 0

        command = command.strip()

        # Split into command name and arguments
        parts = command.split(None, 1)
        cmd_name = parts[0]
        cmd_args = parts[1] if len(parts) > 1 else ""

        # 1. Try magic command dispatch
        if self.registry.has_command(cmd_name):
            try:
                result = self.registry.execute(cmd_name, cmd_args)
                if result is not None:
                    print(result)
                return 0
            except Exception as e:
                print(f"[pybridge] Error in !{cmd_name}: {e}",
                      file=sys.stderr)
                return -1

        # 2. Try Python eval (single expression)
        try:
            result = eval(command, self.context)
            if result is not None:
                print(repr(result))
            return 0
        except SyntaxError:
            pass  # Not an expression, try exec
        except Exception as e:
            print(f"[pybridge] {type(e).__name__}: {e}", file=sys.stderr)
            return -1

        # 3. Fall back to Python exec (statement)
        try:
            exec(command, self.context)
            return 0
        except Exception as e:
            print(f"[pybridge] {type(e).__name__}: {e}", file=sys.stderr)
            traceback.print_exc(file=sys.stderr)
            return -1

    def execute_file(self, path: str) -> int:
        """
        Execute a .bot script file.

        Args:
            path: Path to the script file.

        Returns:
            0 on success, -1 on error.
        """
        if not os.path.isfile(path):
            print(f"[pybridge] File not found: {path}", file=sys.stderr)
            return -1

        try:
            with open(path, "r", encoding="utf-8") as f:
                code = f.read()

            exec(compile(code, path, "exec"), self.context)
            return 0
        except Exception as e:
            print(f"[pybridge] Error in {path}: {e}", file=sys.stderr)
            traceback.print_exc(file=sys.stderr)
            return -1

    def get_completions(self, prefix: str) -> list:
        """
        Get tab-completion suggestions for a partial command.

        Args:
            prefix: Partial command string.

        Returns:
            List of matching command/variable names.
        """
        completions = []

        # Magic commands
        for name in self.registry.list_commands():
            if name.startswith(prefix):
                completions.append(name)

        # Python context variables
        for name in self.context:
            if name.startswith(prefix) and not name.startswith("_"):
                completions.append(name)

        return sorted(set(completions))


# ── Module-level singleton for C bridge access ───────────────

_bridge_instance: Optional[PyBridge] = None


def _get_bridge() -> PyBridge:
    """Get or create the singleton bridge instance."""
    global _bridge_instance
    if _bridge_instance is None:
        _bridge_instance = PyBridge()
    return _bridge_instance


def execute(command: str) -> int:
    """
    Module-level execute function called from C (pybridge.c).
    This is the primary entry point for the C → Python bridge.

    Args:
        command: Command string (! prefix already stripped).

    Returns:
        0 on success, -1 on error.
    """
    return _get_bridge().execute(command)


def execute_file(path: str) -> int:
    """
    Module-level file execution called from C.

    Args:
        path: Path to .bot script file.

    Returns:
        0 on success, -1 on error.
    """
    return _get_bridge().execute_file(path)
