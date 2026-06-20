/* ============================================================
 * BotOS Core — Process Executor
 * ============================================================
 * File:    executor.h
 * Layer:   L4 — Development Layer
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Executes parsed commands by forking child processes,
 * setting up pipes, handling redirections, and managing
 * process lifecycle (wait, signals).
 * ============================================================ */

#ifndef BOTOS_EXECUTOR_H
#define BOTOS_EXECUTOR_H

#include "parser.h"
#include <sys/types.h>  /* pid_t */

#ifdef __cplusplus
extern "C" {
#endif

/* ── Execution Result ────────────────────────────────────── */

/**
 * Result of command execution.
 */
typedef struct exec_result {
    int   exit_code;     /**< Exit status of last command.    */
    int   signaled;      /**< 1 if killed by signal.          */
    int   signal_num;    /**< Signal number (if signaled).    */
    pid_t last_pid;      /**< PID of last child in pipeline.  */
} exec_result_t;

/* ── Executor API ────────────────────────────────────────── */

/**
 * Initialize the executor subsystem.
 * Sets up process group handling and signal masks.
 *
 * @return  0 on success, -1 on error.
 */
int executor_init(void);

/**
 * Execute a full pipeline (possibly multi-stage).
 * Creates pipes between stages, forks child processes,
 * sets up redirections, and waits for completion.
 *
 * @param pipeline  Parsed pipeline to execute.
 * @param result    Output: execution result (may be NULL).
 * @return          Exit code of the last command.
 */
int executor_run_pipeline(const pipeline_t *pipeline, exec_result_t *result);

/**
 * Execute a single command (no pipeline).
 * Convenience wrapper around executor_run_pipeline().
 *
 * @param cmd     Parsed command to execute.
 * @param result  Output: execution result (may be NULL).
 * @return        Exit code of the command.
 */
int executor_run_command(const parsed_cmd_t *cmd, exec_result_t *result);

/**
 * Wait for a background process to finish.
 *
 * @param pid   PID of the background process.
 * @return      Exit code, or -1 on error.
 */
int executor_wait_pid(pid_t pid);

/**
 * Clean up executor resources.
 */
void executor_cleanup(void);

/**
 * List all active background and stopped jobs.
 *
 * @return 0 on success.
 */
int executor_jobs(void);

/**
 * Bring a job to the foreground.
 *
 * @param job_id  The job ID.
 * @return        Exit status of the job, or -1 on error.
 */
int executor_fg(int job_id);

/**
 * Run a stopped job in the background.
 *
 * @param job_id  The job ID.
 * @return        0 on success, -1 on error.
 */
int executor_bg(int job_id);

#ifdef __cplusplus
}
#endif

#endif /* BOTOS_EXECUTOR_H */
