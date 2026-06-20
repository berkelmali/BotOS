/* ============================================================
 * BotOS Core — Command Parser
 * ============================================================
 * File:    parser.h
 * Layer:   L4 — Development Layer
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Tokenizes raw input lines into structured command objects.
 * Handles quoting, escapes, pipes, redirections, and
 * PyBridge prefix detection.
 * ============================================================ */

#ifndef BOTOS_PARSER_H
#define BOTOS_PARSER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ───────────────────────────────────────────── */

#define PARSER_MAX_ARGS      64    /**< Max arguments per command.    */
#define PARSER_MAX_PIPES     8     /**< Max pipe segments in a line.  */

/* ── Redirect Types ──────────────────────────────────────── */

typedef enum {
    REDIR_NONE     = 0,   /**< No redirection.             */
    REDIR_OUT      = 1,   /**< >  stdout to file.          */
    REDIR_APPEND   = 2,   /**< >> stdout append to file.   */
    REDIR_IN       = 3,   /**< <  stdin from file.         */
    REDIR_ERR      = 4,   /**< 2> stderr to file.          */
    REDIR_HEREDOC  = 5,   /**< << heredoc redirect.        */
} redir_type_t;

/* ── Redirect Descriptor ─────────────────────────────────── */

typedef struct {
    redir_type_t type;
    char        *filename;   /**< Target/source filename.   */
} redir_t;

/* ── Parsed Command ──────────────────────────────────────── */

/**
 * A single parsed command (one segment of a pipeline).
 */
typedef struct parsed_cmd {
    char       *argv[PARSER_MAX_ARGS];   /**< Argument vector (NULL-terminated). */
    int         argc;                     /**< Number of arguments.              */
    redir_t     redir_in;                 /**< Input redirection (< file).       */
    redir_t     redir_out;                /**< Output redirection (> or >>).     */
    redir_t     redir_err;                /**< Error redirection (2> file).      */
    int         background;               /**< Run in background (&).           */
    int         is_pybridge;              /**< Starts with '!' prefix.          */
} parsed_cmd_t;

/* ── Pipeline ────────────────────────────────────────────── */

/**
 * A full parsed pipeline: cmd1 | cmd2 | cmd3 ...
 */
typedef struct pipeline {
    parsed_cmd_t  commands[PARSER_MAX_PIPES]; /**< Pipeline segments.       */
    int           cmd_count;                   /**< Number of segments.      */
} pipeline_t;

/* ── Parser API ──────────────────────────────────────────── */

/**
 * Parse a raw input line into a pipeline structure.
 *
 * @param input     Raw input string (will be modified in-place for tokenization).
 * @param pipeline  Output: populated pipeline structure.
 * @return          0 on success, -1 on parse error.
 */
int parser_parse_line(char *input, pipeline_t *pipeline);

/**
 * Expand glob wildcards (*, ?) in a pipeline.
 *
 * @param pipeline  Pipeline containing parsed commands to expand.
 * @return          0 on success, -1 on error.
 */
int parser_expand_pipeline_globs(pipeline_t *pipeline);

/**
 * Free resources allocated during parsing.
 * Must be called after the pipeline is no longer needed.
 *
 * @param pipeline  Pipeline to free.
 */
void parser_free_pipeline(pipeline_t *pipeline);

/**
 * Check if the input line is empty or only whitespace.
 *
 * @param input  Input string to check.
 * @return       1 if empty/whitespace-only, 0 otherwise.
 */
int parser_is_empty(const char *input);

/**
 * Check if the input is a PyBridge command (! prefix).
 *
 * @param input  Input string to check.
 * @return       1 if PyBridge, 0 otherwise.
 */
int parser_is_pybridge(const char *input);

#ifdef __cplusplus
}
#endif

#endif /* BOTOS_PARSER_H */
