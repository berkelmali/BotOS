/* ============================================================
 * BotOS Core — Process Executor (Production)
 * ============================================================
 * File:    executor.c
 * Layer:   L4 — Development Layer
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Phase 3 — Production pipeline executor.
 *
 * Architecture:
 *   - Multi-stage pipeline via pipe() chaining
 *   - Process group management (setpgid) for job control
 *   - File redirections: <, >, >>, 2>
 *   - Background job tracking with [job_id] PID reporting
 *   - Proper signal mask: SIGINT/SIGTSTP restored in children
 *   - EINTR-safe waitpid loop
 *   - Exit code tracking: $? semantics (128+sig for signals)
 * ============================================================ */

#include "executor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

/* ── Background Job Tracking ─────────────────────────────── */

#define EXEC_MAX_BG_JOBS  32

typedef enum {
    JOB_RUNNING,
    JOB_STOPPED
} job_state_t;

typedef struct {
    int         active;
    int         job_id;
    pid_t       pgid;       /**< Process group ID of the pipeline.   */
    pid_t       last_pid;   /**< PID of last command in pipeline.    */
    int         cmd_count;  /**< Number of commands in the pipeline. */
    char        cmdline[256]; /**< First command name for display.   */
    job_state_t state;
} bg_job_t;

static bg_job_t g_bg_jobs[EXEC_MAX_BG_JOBS];
static int      g_next_job_id = 1;

/* ── Executor State ──────────────────────────────────────── */

static int g_executor_initialized = 0;
static int g_last_exit_code = 0;   /**< $? equivalent. */

/* ── Internal: Redirection Setup ─────────────────────────── */

/**
 * Set up file redirections for a child process.
 * Called after fork(), in the child only.
 *
 * Handles:
 *   - REDIR_IN:     stdin  < file
 *   - REDIR_OUT:    stdout > file  (create/truncate)
 *   - REDIR_APPEND: stdout >> file (create/append)
 *   - REDIR_ERR:    stderr 2> file (create/truncate)
 *
 * @return  0 on success, -1 on error (with message to stderr).
 */
static int setup_redirections(const parsed_cmd_t *cmd)
{
    /* ── stdin redirection ────────────────────────────── */
    if (cmd->redir_in.type == REDIR_IN && cmd->redir_in.filename) {
        int fd = open(cmd->redir_in.filename, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "botshell: %s: %s\n",
                    cmd->redir_in.filename, strerror(errno));
            return -1;
        }
        if (dup2(fd, STDIN_FILENO) < 0) {
            close(fd);
            return -1;
        }
        close(fd);
    }

    /* ── stdout redirection (truncate) ────────────────── */
    if (cmd->redir_out.type == REDIR_OUT && cmd->redir_out.filename) {
        int fd = open(cmd->redir_out.filename,
                      O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            fprintf(stderr, "botshell: %s: %s\n",
                    cmd->redir_out.filename, strerror(errno));
            return -1;
        }
        if (dup2(fd, STDOUT_FILENO) < 0) {
            close(fd);
            return -1;
        }
        close(fd);
    }

    /* ── stdout redirection (append) ──────────────────── */
    if (cmd->redir_out.type == REDIR_APPEND && cmd->redir_out.filename) {
        int fd = open(cmd->redir_out.filename,
                      O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd < 0) {
            fprintf(stderr, "botshell: %s: %s\n",
                    cmd->redir_out.filename, strerror(errno));
            return -1;
        }
        if (dup2(fd, STDOUT_FILENO) < 0) {
            close(fd);
            return -1;
        }
        close(fd);
    }

    /* ── stderr redirection ───────────────────────────── */
    if (cmd->redir_err.type == REDIR_ERR && cmd->redir_err.filename) {
        int fd = open(cmd->redir_err.filename,
                      O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            fprintf(stderr, "botshell: %s: %s\n",
                    cmd->redir_err.filename, strerror(errno));
            return -1;
        }
        if (dup2(fd, STDERR_FILENO) < 0) {
            close(fd);
            return -1;
        }
        close(fd);
    }

    return 0;
}

/* ── Internal: Close Pipe FDs ────────────────────────────── */

/**
 * Safely close a file descriptor if it's valid.
 */
static inline void safe_close(int *fd)
{
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

/* ── Internal: Background Job Management ─────────────────── */

static bg_job_t *bg_alloc_job(void)
{
    for (int i = 0; i < EXEC_MAX_BG_JOBS; i++) {
        if (!g_bg_jobs[i].active) {
            g_bg_jobs[i].active = 1;
            g_bg_jobs[i].job_id = g_next_job_id++;
            g_bg_jobs[i].state = JOB_RUNNING;
            return &g_bg_jobs[i];
        }
    }
    return NULL;
}

/**
 * Reap completed background jobs and report their status.
 */
static void bg_reap_completed(void)
{
    int status;
    pid_t pid;

    // Use WUNTRACED | WCONTINUED to receive events for stopped and resumed jobs
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        for (int i = 0; i < EXEC_MAX_BG_JOBS; i++) {
            if (g_bg_jobs[i].active && (g_bg_jobs[i].last_pid == pid || g_bg_jobs[i].pgid == pid)) {
                if (WIFEXITED(status) || WIFSIGNALED(status)) {
                    int code = 0;
                    if (WIFEXITED(status)) {
                        code = WEXITSTATUS(status);
                    } else if (WIFSIGNALED(status)) {
                        code = 128 + WTERMSIG(status);
                    }

                    fprintf(stderr, "[%d]  Done(%d)  %s\n",
                            g_bg_jobs[i].job_id, code, g_bg_jobs[i].cmdline);
                    g_bg_jobs[i].active = 0;
                } else if (WIFSTOPPED(status)) {
                    g_bg_jobs[i].state = JOB_STOPPED;
                    fprintf(stderr, "[%d]  Stopped  %s\n",
                            g_bg_jobs[i].job_id, g_bg_jobs[i].cmdline);
                } else if (WIFCONTINUED(status)) {
                    g_bg_jobs[i].state = JOB_RUNNING;
                    fprintf(stderr, "[%d]  Continued  %s\n",
                            g_bg_jobs[i].job_id, g_bg_jobs[i].cmdline);
                }
                break;
            }
        }
    }
}

/* ── Public API ──────────────────────────────────────────── */

int executor_init(void)
{
    if (g_executor_initialized) return 0;

    memset(g_bg_jobs, 0, sizeof(g_bg_jobs));
    g_next_job_id = 1;
    g_last_exit_code = 0;
    g_executor_initialized = 1;

    return 0;
}

int executor_run_pipeline(const pipeline_t *pipeline, exec_result_t *result)
{
    if (!pipeline || pipeline->cmd_count == 0) {
        errno = EINVAL;
        return -1;
    }

    /* Reap any completed background jobs before starting new work */
    bg_reap_completed();

    int num_cmds = pipeline->cmd_count;
    int is_background = pipeline->commands[num_cmds - 1].background;

    /* Storage for pipe file descriptors between stages */
    int prev_read_fd = -1;

    /* Track all child PIDs for waiting */
    pid_t child_pids[PARSER_MAX_PIPES];
    memset(child_pids, 0, sizeof(child_pids));

    /* Process group ID — first child becomes the leader */
    pid_t pgid = 0;

    for (int i = 0; i < num_cmds; i++) {
        const parsed_cmd_t *cmd = &pipeline->commands[i];
        int pipe_fd[2] = {-1, -1};

        /* Create pipe between this stage and the next */
        if (i < num_cmds - 1) {
            if (pipe(pipe_fd) < 0) {
                perror("botshell: pipe");
                safe_close(&prev_read_fd);
                return -1;
            }
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("botshell: fork");
            safe_close(&prev_read_fd);
            safe_close(&pipe_fd[0]);
            safe_close(&pipe_fd[1]);
            return -1;
        }

        if (pid == 0) {
            /* ════════════════════════════════════════════
             *  CHILD PROCESS
             * ════════════════════════════════════════════ */

            /* Set process group (first child creates the group) */
            if (pgid == 0) {
                setpgid(0, 0);  /* Become group leader */
            } else {
                setpgid(0, pgid);  /* Join existing group */
            }

            /* Restore default signal handlers */
            signal(SIGINT,  SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGPIPE, SIG_DFL);

            /* Wire up pipe: previous stage's output → our stdin */
            if (prev_read_fd >= 0) {
                dup2(prev_read_fd, STDIN_FILENO);
                close(prev_read_fd);
            }

            /* Wire up pipe: our stdout → next stage's input */
            if (pipe_fd[1] >= 0) {
                dup2(pipe_fd[1], STDOUT_FILENO);
                close(pipe_fd[1]);
            }

            /* Close the unused read end of the current pipe */
            if (pipe_fd[0] >= 0) {
                close(pipe_fd[0]);
            }

            /* Apply file redirections (overrides pipes if both set) */
            if (setup_redirections(cmd) < 0) {
                _exit(1);
            }

            /* Execute */
            execvp(cmd->argv[0], cmd->argv);

            /* exec failed — distinguish "not found" vs "permission denied" */
            if (errno == ENOENT) {
                fprintf(stderr, "botshell: %s: command not found\n", cmd->argv[0]);
                _exit(127);
            } else {
                fprintf(stderr, "botshell: %s: %s\n", cmd->argv[0], strerror(errno));
                _exit(126);
            }
        }

        /* ════════════════════════════════════════════════
         *  PARENT PROCESS
         * ════════════════════════════════════════════════ */

        child_pids[i] = pid;

        /* Set the process group from parent side too (race-free) */
        if (i == 0) {
            pgid = pid;
        }
        setpgid(pid, pgid);

        /* Close pipe ends we no longer need */
        safe_close(&pipe_fd[1]);   /* Write end belongs to child now */
        safe_close(&prev_read_fd); /* Previous read end consumed     */

        /* Save current pipe's read end for the next iteration */
        prev_read_fd = pipe_fd[0];
    }

    /* Close the last unused read fd */
    safe_close(&prev_read_fd);

    /* ── Wait for Pipeline Completion ────────────────────── */

    pid_t last_pid = child_pids[num_cmds - 1];
    int exit_code = 0;
    int was_signaled = 0;
    int sig_num = 0;
    int is_interactive = isatty(STDIN_FILENO);

    if (!is_background) {
        /* Foreground: wait for all children in the pipeline */
        int status;

        /* Put the process group in the foreground */
        if (is_interactive) {
            tcsetpgrp(STDIN_FILENO, pgid);
        }

        /* Wait for the last command (determines pipeline exit code) */
        pid_t w;
        do {
            w = waitpid(last_pid, &status, WUNTRACED);
        } while (w < 0 && errno == EINTR);

        /* Return terminal control back to shell */
        if (is_interactive) {
            tcsetpgrp(STDIN_FILENO, getpgrp());
        }

        if (w > 0) {
            if (WIFSTOPPED(status)) {
                /* Job stopped (e.g. Ctrl-Z) */
                bg_job_t *job = bg_alloc_job();
                if (job) {
                    job->pgid      = pgid;
                    job->last_pid  = last_pid;
                    job->cmd_count = num_cmds;
                    job->state     = JOB_STOPPED;
                    if (pipeline->commands[0].argv[0]) {
                        strncpy(job->cmdline, pipeline->commands[0].argv[0],
                                sizeof(job->cmdline) - 1);
                    }
                    fprintf(stderr, "\n[%d]+ Stopped  %s\n", job->job_id, job->cmdline);
                }
                exit_code = 128 + WSTOPSIG(status);
            } else if (WIFEXITED(status)) {
                exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                exit_code = 128 + WTERMSIG(status);
                was_signaled = 1;
                sig_num = WTERMSIG(status);
            }
        }

        /* Reap other children in the pipeline (only if they did not stop) */
        if (w > 0 && !WIFSTOPPED(status)) {
            for (int i = 0; i < num_cmds - 1; i++) {
                if (child_pids[i] > 0) {
                    do {
                        w = waitpid(child_pids[i], NULL, 0);
                    } while (w < 0 && errno == EINTR);
                }
            }
        }

    } else {
        /* Background: register the job and return immediately */
        bg_job_t *job = bg_alloc_job();
        if (job) {
            job->pgid      = pgid;
            job->last_pid  = last_pid;
            job->cmd_count = num_cmds;
            job->state     = JOB_RUNNING;
            if (pipeline->commands[0].argv[0]) {
                strncpy(job->cmdline, pipeline->commands[0].argv[0],
                        sizeof(job->cmdline) - 1);
            }
            fprintf(stderr, "[%d] %d\n", job->job_id, (int)last_pid);
        } else {
            fprintf(stderr, "[bg] %d\n", (int)last_pid);
        }
    }

    /* Update $? */
    g_last_exit_code = exit_code;

    /* Fill result */
    if (result) {
        result->exit_code  = exit_code;
        result->last_pid   = last_pid;
        result->signaled   = was_signaled;
        result->signal_num = sig_num;
    }

    return exit_code;
}

int executor_run_command(const parsed_cmd_t *cmd, exec_result_t *result)
{
    if (!cmd || cmd->argc == 0) return -1;

    pipeline_t pipeline;
    memset(&pipeline, 0, sizeof(pipeline));
    memcpy(&pipeline.commands[0], cmd, sizeof(parsed_cmd_t));
    pipeline.cmd_count = 1;

    return executor_run_pipeline(&pipeline, result);
}

int executor_wait_pid(pid_t pid)
{
    if (pid <= 0) return -1;

    int status;
    pid_t w;

    do {
        w = waitpid(pid, &status, 0);
    } while (w < 0 && errno == EINTR);

    if (w < 0) return -1;

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return -1;
}

void executor_cleanup(void)
{
    /* Reap all remaining background jobs */
    for (int i = 0; i < EXEC_MAX_BG_JOBS; i++) {
        if (g_bg_jobs[i].active && g_bg_jobs[i].last_pid > 0) {
            kill(g_bg_jobs[i].last_pid, SIGTERM);
        }
    }

    /* Wait briefly for termination */
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0) { /* drain */ }

    memset(g_bg_jobs, 0, sizeof(g_bg_jobs));
    g_executor_initialized = 0;
}

int executor_jobs(void)
{
    int count = 0;
    for (int i = 0; i < EXEC_MAX_BG_JOBS; i++) {
        if (g_bg_jobs[i].active) {
            const char *state_str = (g_bg_jobs[i].state == JOB_STOPPED) ? "Stopped" : "Running";
            printf("[%d]  %s  %s\n", g_bg_jobs[i].job_id, state_str, g_bg_jobs[i].cmdline);
            count++;
        }
    }
    if (count == 0) {
        printf("  (no background jobs)\n");
    }
    return 0;
}

int executor_fg(int job_id)
{
    bg_job_t *job = NULL;
    for (int i = 0; i < EXEC_MAX_BG_JOBS; i++) {
        if (g_bg_jobs[i].active && g_bg_jobs[i].job_id == job_id) {
            job = &g_bg_jobs[i];
            break;
        }
    }

    if (!job) {
        fprintf(stderr, "botshell: fg: no such job: %d\n", job_id);
        return -1;
    }

    printf("%s\n", job->cmdline);
    fflush(stdout);

    int is_interactive = isatty(STDIN_FILENO);

    /* Send SIGCONT to process group */
    kill(-job->pgid, SIGCONT);

    if (is_interactive) {
        tcsetpgrp(STDIN_FILENO, job->pgid);
    }

    int status;
    pid_t w;
    do {
        w = waitpid(job->last_pid, &status, WUNTRACED);
    } while (w < 0 && errno == EINTR);

    if (is_interactive) {
        tcsetpgrp(STDIN_FILENO, getpgrp());
    }

    int exit_code = 0;
    if (w > 0) {
        if (WIFSTOPPED(status)) {
            job->state = JOB_STOPPED;
            fprintf(stderr, "\n[%d]+ Stopped  %s\n", job->job_id, job->cmdline);
            exit_code = 128 + WSTOPSIG(status);
        } else {
            /* Finished */
            job->active = 0;
            if (WIFEXITED(status)) {
                exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                exit_code = 128 + WTERMSIG(status);
            }
        }
    }

    g_last_exit_code = exit_code;
    return exit_code;
}

int executor_bg(int job_id)
{
    bg_job_t *job = NULL;
    for (int i = 0; i < EXEC_MAX_BG_JOBS; i++) {
        if (g_bg_jobs[i].active && g_bg_jobs[i].job_id == job_id) {
            job = &g_bg_jobs[i];
            break;
        }
    }

    if (!job) {
        fprintf(stderr, "botshell: bg: no such job: %d\n", job_id);
        return -1;
    }

    printf("[%d] %s &\n", job->job_id, job->cmdline);
    fflush(stdout);

    /* Send SIGCONT to process group */
    kill(-job->pgid, SIGCONT);
    job->state = JOB_RUNNING;

    return 0;
}
