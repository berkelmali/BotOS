/* ============================================================
 * BotOS Core — Built-in Shell Commands
 * ============================================================
 * File:    builtins.h
 * Layer:   L4 — Development Layer
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Built-in commands are executed directly by BotShell without
 * forking a child process. They have access to shell internals
 * (e.g., cd changes the shell's own working directory).
 * ============================================================ */

#ifndef BOTOS_BUILTINS_H
#define BOTOS_BUILTINS_H

#include "parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Built-in Command Handler Type ───────────────────────── */

/**
 * Function signature for built-in command handlers.
 *
 * @param cmd  Parsed command with argv/argc.
 * @return     Exit status (0 = success).
 */
typedef int (*builtin_handler_t)(const parsed_cmd_t *cmd);

/* ── Built-in Registry ───────────────────────────────────── */

/**
 * Check if a command name corresponds to a built-in.
 *
 * @param name  Command name (e.g., "cd", "exit").
 * @return      1 if built-in, 0 otherwise.
 */
int builtin_is_builtin(const char *name);

/**
 * Execute a built-in command.
 *
 * @param cmd  Parsed command whose argv[0] is a built-in.
 * @return     Exit status of the built-in.
 */
int builtin_execute(const parsed_cmd_t *cmd);

/* ── Individual Built-in Declarations ────────────────────── */

int builtin_cd(const parsed_cmd_t *cmd);      /**< Change directory.     */
int builtin_exit(const parsed_cmd_t *cmd);     /**< Exit the shell.      */
int builtin_help(const parsed_cmd_t *cmd);     /**< Print help info.     */
int builtin_clear(const parsed_cmd_t *cmd);    /**< Clear terminal.      */
int builtin_history(const parsed_cmd_t *cmd);  /**< Show command history. */
int builtin_export(const parsed_cmd_t *cmd);   /**< Set env variable.    */
int builtin_pwd(const parsed_cmd_t *cmd);      /**< Print working dir.   */
int builtin_echo(const parsed_cmd_t *cmd);     /**< Echo arguments.      */
int builtin_jobs(const parsed_cmd_t *cmd);     /**< List background jobs. */
int builtin_fg(const parsed_cmd_t *cmd);       /**< Foreground a job.    */
int builtin_bg(const parsed_cmd_t *cmd);       /**< Background a job.    */
int builtin_neofetch(const parsed_cmd_t *cmd); /**< Show system info.    */
int builtin_matrix(const parsed_cmd_t *cmd);   /**< Matrix rain screen.  */
int builtin_adventure(const parsed_cmd_t *cmd);/**< Text adventure game. */
int builtin_whoami(const parsed_cmd_t *cmd);   /**< Print current user.   */
int builtin_uptime(const parsed_cmd_t *cmd);   /**< Show system uptime.   */
int builtin_calc(const parsed_cmd_t *cmd);     /**< Simple calculator.    */
int builtin_cowsay(const parsed_cmd_t *cmd);   /**< Talkative ASCII cow.  */
int builtin_tuxsay(const parsed_cmd_t *cmd);   /**< Talkative ASCII penguin. */
int builtin_dashboard(const parsed_cmd_t *cmd);/**< Interactive system dashboard. */
int builtin_sl(const parsed_cmd_t *cmd);       /**< Steam Locomotive ASCII animation. */
int builtin_rpg(const parsed_cmd_t *cmd);      /**< RPG main command interface. */
int builtin_hero(const parsed_cmd_t *cmd);     /**< RPG hero character sheet shortcut. */
int builtin_mining(const parsed_cmd_t *cmd);   /**< RPG mining action shortcut. */
int builtin_get_all_names(const char **names, int max_names);

#ifdef __cplusplus
}
#endif

#endif /* BOTOS_BUILTINS_H */
