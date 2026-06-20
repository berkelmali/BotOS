/* ============================================================
 * BotOS Core — PyBridge Python Interface
 * ============================================================
 * File:    pybridge.h
 * Layer:   L4 — Development Layer
 * Author:  Berk Elmalı
 * License: MIT
 *
 * The PyBridge engine bridges BotShell (C) with a Python
 * runtime for high-level scripting. Commands prefixed with '!'
 * are dispatched through this module.
 * ============================================================ */

#ifndef BOTOS_PYBRIDGE_H
#define BOTOS_PYBRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ── PyBridge Lifecycle ──────────────────────────────────── */

/**
 * Initialize the PyBridge Python runtime.
 * Calls Py_Initialize() and loads the bridge module.
 *
 * @param scripts_dir  Path to pybridge/runtime/ directory.
 * @return             0 on success, -1 on error.
 */
int pybridge_init(const char *scripts_dir);

/**
 * Shut down the PyBridge runtime.
 * Calls Py_Finalize() and releases resources.
 */
void pybridge_shutdown(void);

/**
 * Check if PyBridge is available and initialized.
 *
 * @return  1 if ready, 0 if not.
 */
int pybridge_is_available(void);

/* ── Command Execution ───────────────────────────────────── */

/**
 * Execute a PyBridge command.
 * The '!' prefix should already be stripped from the input.
 *
 * @param command  Python/magic command string.
 * @return         0 on success, -1 on error.
 */
int pybridge_execute(const char *command);

/**
 * Execute a .bot script file through the PyBridge runtime.
 *
 * @param path  Path to the .bot script file.
 * @return      0 on success, -1 on error.
 */
int pybridge_exec_script(const char *path);

/* ── Magic Command Registration ──────────────────────────── */

/**
 * Callback type for native magic commands registered from C.
 *
 * @param args  Argument string passed after the command name.
 * @return      0 on success, -1 on error.
 */
typedef int (*magic_cmd_handler_t)(const char *args);

/**
 * Register a native magic command with the PyBridge engine.
 * These are accessible via !command_name in BotShell.
 *
 * @param name     Command name (without '!' prefix).
 * @param handler  C callback function.
 * @param help     Short help text for the command.
 * @return         0 on success, -1 on error.
 */
int pybridge_register_magic(const char *name,
                            magic_cmd_handler_t handler,
                            const char *help);

#ifdef __cplusplus
}
#endif

#endif /* BOTOS_PYBRIDGE_H */
