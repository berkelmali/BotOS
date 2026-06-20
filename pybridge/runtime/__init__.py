"""
BotOS Core — PyBridge Runtime Package
=========================================
Layer:   L4 — Development Layer
Author:  Berk Elmalı
License: MIT

The PyBridge runtime provides the Python-side bridge for
BotShell's '!' command prefix. It receives commands from the
C bridge (pybridge.c), dispatches them to magic command
handlers, and returns structured results.

Architecture:
    BotShell (C) → pybridge.c → bridge.py → magic_commands.py
"""

__version__ = "0.3.0"
__author__ = "Berk Elmalı"

from .bridge import execute, PyBridge
from .magic_commands import MagicRegistry

__all__ = [
    "execute",
    "PyBridge",
    "MagicRegistry",
]
