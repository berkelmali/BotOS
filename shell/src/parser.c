/* ============================================================
 * BotOS Core — Command Parser (Production)
 * ============================================================
 * File:    parser.c
 * Layer:   L4 — Development Layer
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Phase 3 — Production-grade shell command parser.
 *
 * Features:
 *   - Single-quote (literal), double-quote (escape-aware)
 *   - Backslash escapes in unquoted and double-quoted tokens
 *   - Environment variable expansion ($VAR, ${VAR})
 *   - Tilde expansion (~, ~/path)
 *   - Pipe chaining: cmd1 | cmd2 | cmd3
 *   - File redirections: <, >, >>, 2>
 *   - Background operator: &
 *   - PyBridge prefix detection: !
 *   - Comment stripping: # to end of line
 * ============================================================ */

#include "parser.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <glob.h>

/* ── Internal: Expansion Buffer ──────────────────────────── */

/**
 * Static expansion buffer for variable/tilde expansion.
 * We use a fixed buffer to avoid heap allocation per-token.
 */
#define EXPAND_BUF_SIZE   4096

static char g_expand_buf[EXPAND_BUF_SIZE];

/* ── Internal: Whitespace ────────────────────────────────── */

static char *skip_whitespace(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

/* ── Internal: Comment Stripping ─────────────────────────── */

/**
 * Strip comments from input: everything from unquoted # to EOL.
 */
static void strip_comments(char *input)
{
    int in_single = 0;
    int in_double = 0;

    for (char *s = input; *s; s++) {
        if (*s == '\'' && !in_double) {
            in_single = !in_single;
        } else if (*s == '"' && !in_single) {
            in_double = !in_double;
        } else if (*s == '\\' && !in_single) {
            if (*(s + 1)) s++;  /* Skip escaped char */
        } else if (*s == '#' && !in_single && !in_double) {
            *s = '\0';
            return;
        }
    }
}

/* ── Internal: Environment Variable Expansion ────────────── */

/**
 * Expand $VAR and ${VAR} in a string.
 * Writes result into g_expand_buf and returns it.
 * Returns the original string if no expansion needed.
 */
static char *expand_variables(char *token)
{
    /* Quick check: any $ at all? */
    if (!strchr(token, '$')) return token;

    /* Copy token to avoid overlap if token points to g_expand_buf */
    char temp[EXPAND_BUF_SIZE];
    strncpy(temp, token, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    char *dst = g_expand_buf;
    char *end = g_expand_buf + EXPAND_BUF_SIZE - 1;
    char *s = temp;

    while (*s && dst < end) {
        if (*s == '\\' && *(s + 1) == '$') {
            /* Escaped dollar sign */
            *dst++ = '$';
            s += 2;
            continue;
        }

        if (*s != '$') {
            *dst++ = *s++;
            continue;
        }

        /* $ found — expand variable */
        s++;  /* Skip $ */

        char var_name[256];
        int vi = 0;

        if (*s == '{') {
            /* ${VAR} form */
            s++;
            while (*s && *s != '}' && vi < 255) {
                var_name[vi++] = *s++;
            }
            if (*s == '}') s++;
        } else if (*s == '?') {
            /* $? — last exit code (special case, handled by shell) */
            var_name[vi++] = '?';
            s++;
        } else {
            /* $VAR form */
            while (*s && (isalnum((unsigned char)*s) || *s == '_') && vi < 255) {
                var_name[vi++] = *s++;
            }
        }

        var_name[vi] = '\0';

        /* Look up the variable */
        const char *val = getenv(var_name);
        if (val) {
            while (*val && dst < end) {
                *dst++ = *val++;
            }
        }
        /* If not found, expand to empty string */
    }

    *dst = '\0';

    /* Return the expanded buffer (caller must use before next call) */
    return g_expand_buf;
}

/* ── Internal: Tilde Expansion ───────────────────────────── */

/**
 * Expand ~ at the start of a token to $HOME.
 */
static char *expand_tilde(char *token)
{
    if (token[0] != '~') return token;

    /* ~ alone or ~/path */
    if (token[1] == '\0' || token[1] == '/') {
        const char *home = getenv("HOME");
        if (!home) home = "/root";

        snprintf(g_expand_buf, EXPAND_BUF_SIZE, "%s%s", home, token + 1);
        return g_expand_buf;
    }

    /* ~user form — not supported, return as-is */
    return token;
}

/* ── Internal: Tokenizer ─────────────────────────────────── */

/**
 * Extract the next token from input, handling:
 *   - Double quotes with escape processing
 *   - Single quotes (literal, no escapes)
 *   - Backslash escapes in unquoted context
 *   - Stops at whitespace, pipe, redirect, ampersand
 *
 * Modifies the input string in-place.
 * Advances *input past the consumed token.
 *
 * @return  Pointer to the token start, or NULL on EOL.
 */
static char *next_token(char **input)
{
    char *s = skip_whitespace(*input);
    if (*s == '\0') return NULL;

    char *token_start;

    if (*s == '"') {
        /* ── Double-quoted string ────────────────────── */
        s++;
        token_start = s;
        char *dst = s;  /* In-place unescape */

        while (*s && *s != '"') {
            if (*s == '\\' && *(s + 1)) {
                s++;  /* Skip backslash */
                switch (*s) {
                    case 'n':  *dst++ = '\n'; break;
                    case 't':  *dst++ = '\t'; break;
                    case '\\': *dst++ = '\\'; break;
                    case '"':  *dst++ = '"';  break;
                    case '$':  *dst++ = '$';  break;
                    default:   *dst++ = *s;   break;
                }
                s++;
            } else {
                *dst++ = *s++;
            }
        }
        if (*s == '"') s++;
        *dst = '\0';

    } else if (*s == '\'') {
        /* ── Single-quoted string (no escapes) ───────── */
        s++;
        token_start = s;
        while (*s && *s != '\'') s++;
        if (*s == '\'') *s++ = '\0';

    } else {
        /* ── Unquoted token ──────────────────────────── */
        token_start = s;

        while (*s && !isspace((unsigned char)*s) &&
               *s != '|' && *s != '>' && *s != '<' && *s != '&') {

            if (*s == '\\' && *(s + 1)) {
                /* Backslash escape: remove the backslash, keep the char */
                memmove(s, s + 1, strlen(s));
                s++;
            } else {
                s++;
            }
        }

        if (*s && !isspace((unsigned char)*s)) {
            /* Special char — don't null-terminate, let caller handle */
        } else if (*s) {
            *s++ = '\0';
        }
    }

    *input = s;
    return token_start;
}

/* ── Internal: Redirection Parser ────────────────────────── */

static int parse_redirections(char **pos, parsed_cmd_t *cmd)
{
    char *s = *pos;

    while (*s) {
        s = skip_whitespace(s);
        if (*s == '\0' || *s == '|' || *s == '&') break;

        if (*s == '>' && *(s + 1) == '>') {
            /* >> append */
            s += 2;
            cmd->redir_out.type = REDIR_APPEND;
            char *fname = next_token(&s);
            cmd->redir_out.filename = fname ? strdup(fname) : NULL;
        } else if (*s == '>') {
            /* > truncate */
            s++;
            cmd->redir_out.type = REDIR_OUT;
            char *fname = next_token(&s);
            cmd->redir_out.filename = fname ? strdup(fname) : NULL;
        } else if (*s == '<' && *(s + 1) == '<') {
            /* << heredoc */
            s += 2;
            cmd->redir_in.type = REDIR_HEREDOC;
            char *fname = next_token(&s);
            cmd->redir_in.filename = fname ? strdup(fname) : NULL;
        } else if (*s == '<') {
            /* < input */
            s++;
            cmd->redir_in.type = REDIR_IN;
            char *fname = next_token(&s);
            cmd->redir_in.filename = fname ? strdup(fname) : NULL;
        } else if (*s == '2' && *(s + 1) == '>') {
            /* 2> stderr */
            s += 2;
            cmd->redir_err.type = REDIR_ERR;
            char *fname = next_token(&s);
            cmd->redir_err.filename = fname ? strdup(fname) : NULL;
        } else {
            break;
        }
    }

    *pos = s;
    return 0;
}

/* ── Public API ──────────────────────────────────────────── */

int parser_is_empty(const char *input)
{
    if (!input) return 1;
    while (*input) {
        if (!isspace((unsigned char)*input)) return 0;
        input++;
    }
    return 1;
}

int parser_is_pybridge(const char *input)
{
    if (!input) return 0;
    const char *s = input;
    while (*s && isspace((unsigned char)*s)) s++;
    return (*s == '!');
}

int parser_parse_line(char *input, pipeline_t *pipeline)
{
    if (!input || !pipeline) return -1;

    memset(pipeline, 0, sizeof(pipeline_t));

    /* Strip comments before parsing */
    strip_comments(input);

    /* Quick empty check after comment strip */
    if (parser_is_empty(input)) return -1;

    char *pos = input;
    int   cmd_idx = 0;

    while (*pos && cmd_idx < PARSER_MAX_PIPES) {
        parsed_cmd_t *cmd = &pipeline->commands[cmd_idx];
        memset(cmd, 0, sizeof(parsed_cmd_t));

        /* Check for PyBridge prefix */
        char *check = skip_whitespace(pos);
        if (*check == '!') {
            cmd->is_pybridge = 1;
        }

        /* Tokenize arguments until pipe, background, or end */
        while (*pos) {
            pos = skip_whitespace(pos);
            if (*pos == '\0') break;

            /* Pipe: advance to next pipeline segment */
            if (*pos == '|') {
                pos++;
                break;
            }

            /* Background operator */
            if (*pos == '&') {
                cmd->background = 1;
                pos++;
                continue;
            }

            /* Redirections */
            if (*pos == '>' || *pos == '<' ||
                (*pos == '2' && *(pos + 1) == '>')) {
                parse_redirections(&pos, cmd);
                continue;
            }

            /* Regular argument */
            char *token = next_token(&pos);
            if (!token) break;

            /* Apply expansions */
            token = expand_tilde(token);
            token = expand_variables(token);

            if (cmd->argc < PARSER_MAX_ARGS - 1) {
                cmd->argv[cmd->argc++] = strdup(token);
            }
        }

        /* Null-terminate argv */
        cmd->argv[cmd->argc] = NULL;

        if (cmd->argc > 0) {
            cmd_idx++;
        }
    }

    pipeline->cmd_count = cmd_idx;
    if (cmd_idx > 0) {
        return 0;
    } else {
        parser_free_pipeline(pipeline);
        return -1;
    }
}

void parser_free_pipeline(pipeline_t *pipeline)
{
    if (!pipeline) return;
    for (int c = 0; c < pipeline->cmd_count; c++) {
        parsed_cmd_t *cmd = &pipeline->commands[c];
        for (int i = 0; i < cmd->argc; i++) {
            free(cmd->argv[i]);
            cmd->argv[i] = NULL;
        }
        if (cmd->redir_in.filename) {
            free(cmd->redir_in.filename);
            cmd->redir_in.filename = NULL;
        }
        if (cmd->redir_out.filename) {
            free(cmd->redir_out.filename);
            cmd->redir_out.filename = NULL;
        }
        if (cmd->redir_err.filename) {
            free(cmd->redir_err.filename);
            cmd->redir_err.filename = NULL;
        }
    }
    memset(pipeline, 0, sizeof(pipeline_t));
}

int parser_expand_pipeline_globs(pipeline_t *pipeline)
{
    if (!pipeline) return -1;

    for (int c = 0; c < pipeline->cmd_count; c++) {
        parsed_cmd_t *cmd = &pipeline->commands[c];
        
        char *new_argv[PARSER_MAX_ARGS];
        int new_argc = 0;
        
        for (int i = 0; i < cmd->argc; i++) {
            char *arg = cmd->argv[i];
            
            if (arg && (strchr(arg, '*') || strchr(arg, '?'))) {
                glob_t globbuf;
                memset(&globbuf, 0, sizeof(globbuf));
                
                int r = glob(arg, GLOB_NOCHECK | GLOB_TILDE, NULL, &globbuf);
                if (r == 0) {
                    for (size_t g = 0; g < globbuf.gl_pathc; g++) {
                        if (new_argc < PARSER_MAX_ARGS - 1) {
                            new_argv[new_argc++] = strdup(globbuf.gl_pathv[g]);
                        }
                    }
                } else {
                    if (new_argc < PARSER_MAX_ARGS - 1) {
                        new_argv[new_argc++] = strdup(arg);
                    }
                }
                globfree(&globbuf);
            } else {
                if (new_argc < PARSER_MAX_ARGS - 1) {
                    new_argv[new_argc++] = strdup(arg);
                }
            }
        }
        
        // Free old arguments
        for (int i = 0; i < cmd->argc; i++) {
            free(cmd->argv[i]);
            cmd->argv[i] = NULL;
        }
        
        // Copy new arguments
        cmd->argc = new_argc;
        for (int i = 0; i < new_argc; i++) {
            cmd->argv[i] = new_argv[i];
        }
        cmd->argv[new_argc] = NULL;
    }
    
    return 0;
}
