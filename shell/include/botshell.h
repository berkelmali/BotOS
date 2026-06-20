/* ============================================================
 * BotOS Core — BotShell Main Interface
 * ============================================================
 * File:    botshell.h
 * Layer:   L4 — Development Layer
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Top-level shell interface. Defines the REPL loop, prompt
 * rendering, configuration, and signal handling entry points.
 * ============================================================ */

#ifndef BOTOS_SHELL_H
#define BOTOS_SHELL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ───────────────────────────────────────────── */

#define BOTSHELL_VERSION       "0.3.0"
#define BOTSHELL_MAX_INPUT     4096   /**< Max input line length.      */
#define BOTSHELL_MAX_HISTORY   1000   /**< Max history entries.        */
#define BOTSHELL_PROMPT_MAX    128    /**< Max prompt string length.   */

/* ── Shell Configuration ─────────────────────────────────── */

/**
 * Shell runtime configuration.
 */
typedef struct botshell_config {
    char   prompt[BOTSHELL_PROMPT_MAX]; /**< Prompt format string.     */
    int    pybridge_enabled;             /**< Enable ! prefix commands. */
    int    color_enabled;                /**< Enable colored output.    */
    int    verbose;                      /**< Verbose error messages.   */
    char  *history_file;                 /**< Path to history file.     */
} botshell_config_t;

/* ── Shell Lifecycle ─────────────────────────────────────── */

/**
 * Initialize the BotShell subsystem.
 * Sets up signal handlers, loads config, initializes history.
 *
 * @param config  Shell config (NULL for defaults).
 * @return        0 on success, -1 on error.
 */
int botshell_init(const botshell_config_t *config);

/**
 * Run the main REPL (Read-Eval-Print Loop).
 * Blocks until the user exits (via 'exit' or EOF).
 *
 * @return  Exit status code.
 */
int botshell_run(void);

/**
 * Execute a single command string (non-interactive).
 * Used for -c flag and .bot script execution.
 *
 * @param command  The command string to execute.
 * @return         Exit status of the command.
 */
int botshell_exec_string(const char *command);

/**
 * Execute a .bot script file.
 *
 * @param path  Path to the .bot script file.
 * @return      0 on success, -1 on error.
 */
int botshell_exec_script(const char *path);

/**
 * Clean up and shut down BotShell.
 * Saves history, frees resources.
 */
void botshell_shutdown(void);

/**
 * Print the shell prompt to stdout.
 */
void botshell_print_prompt(void);

/**
 * Get the command history array.
 *
 * @param count_out  Output: number of history entries.
 * @return           Array of history strings (read-only).
 */
char **botshell_get_history(int *count_out);

/**
 * Clear all command history (both in memory and persistent file).
 */
void botshell_clear_history(void);

#ifdef __cplusplus
}
#endif

#endif /* BOTOS_SHELL_H */
