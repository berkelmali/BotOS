/* ============================================================
 * BotOS Core — BotShell REPL (Production)
 * ============================================================
 * File:    botshell.c
 * Layer:   L4 — Development Layer
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Phase 3 — Production shell REPL.
 *
 * Features:
 *   - Interactive REPL with colored prompt (user@host:cwd$)
 *   - History management with file persistence
 *   - SIGINT handling (prints new prompt, doesn't exit)
 *   - Script mode (-c command, file.bot)
 *   - PyBridge integration (! prefix → Python)
 *   - Built-in command dispatch (cd, exit, help, etc.)
 *   - Login banner with system status
 *   - Exit code tracking ($?)
 * ============================================================ */

#include "botshell.h"
#include "parser.h"
#include "executor.h"
#include "builtins.h"
#include "pybridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <termios.h>
#include <dirent.h>
#include <ctype.h>

/* ── Shell State ─────────────────────────────────────────── */

static botshell_config_t g_shell_config;
static volatile int      g_running       = 0;
static int               g_last_exit     = 0;
static int               g_command_count = 0;
static char             *g_history[BOTSHELL_MAX_HISTORY];
static int               g_history_count = 0;
static int               g_interactive   = 0;

/* ── Default Configuration ───────────────────────────────── */

static const botshell_config_t g_default_config = {
    .prompt               = "botshell",
    .pybridge_enabled     = 1,
    .color_enabled        = 1,
    .verbose              = 0,
    .history_file         = NULL,
};

/* ── Color Codes ─────────────────────────────────────────── */

#define CLR_RESET   "\033[0m"
#define CLR_BOLD    "\033[1m"
#define CLR_CYAN    "\033[36m"
#define CLR_GREEN   "\033[32m"
#define CLR_RED     "\033[31m"
#define CLR_YELLOW  "\033[33m"
#define CLR_DIM     "\033[2m"
#define CLR_MAGENTA "\033[35m"

/* ── Signal Handling ─────────────────────────────────────── */

/**
 * SIGINT handler for interactive mode.
 * Cancels the current input line and prints a fresh prompt.
 * Does NOT exit the shell (unlike default behavior).
 */
static void shell_sigint_handler(int sig)
{
    (void)sig;
    /* Write a newline (printf is not signal-safe, but write is) */
    const char nl[] = "\n";
    write(STDOUT_FILENO, nl, 1);

    /* We can't call botshell_print_prompt() here (not signal-safe).
     * The REPL loop will handle reprinting after fgets returns. */
}

/* ── History Management ──────────────────────────────────── */

static void history_add(const char *line)
{
    if (!line || parser_is_empty(line)) return;

    /* Don't add duplicate of the last entry */
    if (g_history_count > 0 &&
        strcmp(g_history[g_history_count - 1], line) == 0) {
        return;
    }

    if (g_history_count >= BOTSHELL_MAX_HISTORY) {
        /* Drop oldest entry */
        free(g_history[0]);
        memmove(g_history, g_history + 1,
                (size_t)(BOTSHELL_MAX_HISTORY - 1) * sizeof(char *));
        g_history_count--;
    }

    g_history[g_history_count] = strdup(line);
    if (g_history[g_history_count]) {
        g_history_count++;
    }
}

static void history_free(void)
{
    for (int i = 0; i < g_history_count; i++) {
        free(g_history[i]);
        g_history[i] = NULL;
    }
    g_history_count = 0;
}

/**
 * Save history to a file.
 */
static void history_save(void)
{
    if (!g_shell_config.history_file || g_history_count == 0) return;

    FILE *fp = fopen(g_shell_config.history_file, "w");
    if (!fp) return;

    /* Save last 500 entries max */
    int start = (g_history_count > 500) ? g_history_count - 500 : 0;
    for (int i = start; i < g_history_count; i++) {
        if (g_history[i]) {
            fprintf(fp, "%s\n", g_history[i]);
        }
    }

    fclose(fp);
}

/**
 * Load history from a file.
 */
static void history_load(void)
{
    if (!g_shell_config.history_file) return;

    FILE *fp = fopen(g_shell_config.history_file, "r");
    if (!fp) return;

    char line[BOTSHELL_MAX_INPUT];
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }
        if (!parser_is_empty(line)) {
            history_add(line);
        }
    }

    fclose(fp);
}

/* ── Public API ──────────────────────────────────────────── */

int botshell_init(const botshell_config_t *config)
{
    if (config) {
        memcpy(&g_shell_config, config, sizeof(botshell_config_t));
    } else {
        memcpy(&g_shell_config, &g_default_config, sizeof(botshell_config_t));
    }

    /* Auto-detect if stdout is a terminal */
    g_interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);

    /* Disable colors if not a terminal */
    if (!g_interactive) {
        g_shell_config.color_enabled = 0;
    }

    /* Initialize subsystems */
    executor_init();

    /* Initialize PyBridge if enabled */
    if (g_shell_config.pybridge_enabled) {
        const char *ag_dir = getenv("BOTOS_PYBRIDGE_DIR");
        if (!ag_dir) ag_dir = "/usr/share/botos/pybridge";

        if (pybridge_init(ag_dir) != 0) {
            if (g_shell_config.verbose) {
                fprintf(stderr,
                    "[botshell] Warning: PyBridge init failed (! commands unavailable)\n");
            }
        }
    }

    /* Install signal handlers for interactive mode */
    if (g_interactive) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = shell_sigint_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;  /* NOT SA_RESTART — lets fgets return NULL */
        sigaction(SIGINT, &sa, NULL);

        /* Ignore SIGQUIT, SIGTSTP, SIGTTOU, and SIGTTIN in interactive shell */
        signal(SIGQUIT, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
    }

    /* Load history */
    history_load();

    g_running       = 1;
    g_last_exit     = 0;
    g_command_count = 0;

    return 0;
}

void botshell_print_prompt(void)
{
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        strncpy(cwd, "?", sizeof(cwd));
    }

    /* Shorten home directory to ~ */
    const char *home = getenv("HOME");
    char display_cwd[1024];

    if (home && strncmp(cwd, home, strlen(home)) == 0) {
        snprintf(display_cwd, sizeof(display_cwd), "~%s", cwd + strlen(home));
    } else {
        strncpy(display_cwd, cwd, sizeof(display_cwd));
    }

    if (g_shell_config.color_enabled) {
        const char *status_indicator = (g_last_exit == 0) ? CLR_GREEN "->" CLR_RESET : CLR_RED "->" CLR_RESET;
        printf("%s%s[%s]%s %s%s%s %s ",
               CLR_BOLD, CLR_CYAN, "root@botos", CLR_RESET,
               CLR_GREEN, display_cwd, CLR_RESET,
               status_indicator);
    } else {
        printf("[root@botos] %s -> ", display_cwd);
    }

    fflush(stdout);
}

char **botshell_get_history(int *count_out)
{
    if (count_out) *count_out = g_history_count;
    return g_history;
}

void botshell_clear_history(void)
{
    history_free();
    history_save();
}

/* ── Tab Completion & Block Parsing Support ──────────────── */

static void find_completions(const char *prefix, int is_command, char ***matches_out, int *count_out)
{
    char **matches = NULL;
    int count = 0;

    #define ADD_MATCH(str) do { \
        matches = realloc(matches, (size_t)(count + 1) * sizeof(char *)); \
        matches[count++] = strdup(str); \
    } while(0)

    if (is_command) {
        /* 1. Check builtins */
        const char *builtin_names[64];
        int b_cnt = builtin_get_all_names(builtin_names, 64);
        for (int i = 0; i < b_cnt; i++) {
            if (strncmp(builtin_names[i], prefix, strlen(prefix)) == 0) {
                ADD_MATCH(builtin_names[i]);
            }
        }
        
        /* 2. Check executables in /usr/bin and /bin */
        const char *paths[] = { "/usr/bin", "/bin" };
        for (int p = 0; p < 2; p++) {
            DIR *dp = opendir(paths[p]);
            if (dp) {
                struct dirent *de;
                while ((de = readdir(dp)) != NULL) {
                    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
                        continue;
                    }
                    if (strncmp(de->d_name, prefix, strlen(prefix)) == 0) {
                        ADD_MATCH(de->d_name);
                    }
                }
                closedir(dp);
            }
        }
    } else {
        /* Path completion */
        char dir_path[1024] = ".";
        const char *file_prefix = prefix;
        
        const char *last_slash = strrchr(prefix, '/');
        if (last_slash) {
            size_t dlen = (size_t)(last_slash - prefix);
            if (dlen == 0) {
                strcpy(dir_path, "/");
            } else {
                memcpy(dir_path, prefix, dlen);
                dir_path[dlen] = '\0';
            }
            file_prefix = last_slash + 1;
        }
        
        DIR *dp = opendir(dir_path);
        if (dp) {
            struct dirent *de;
            while ((de = readdir(dp)) != NULL) {
                if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
                    continue;
                }
                if (strncmp(de->d_name, file_prefix, strlen(file_prefix)) == 0) {
                    char match_name[2048];
                    if (last_slash) {
                        if (strcmp(dir_path, "/") == 0) {
                            snprintf(match_name, sizeof(match_name), "/%s", de->d_name);
                        } else {
                            snprintf(match_name, sizeof(match_name), "%s/%s", dir_path, de->d_name);
                        }
                    } else {
                        strcpy(match_name, de->d_name);
                    }
                    
                    char full_path[4096];
                    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, de->d_name);
                    struct stat st;
                    if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
                        strcat(match_name, "/");
                    }
                    ADD_MATCH(match_name);
                }
            }
            closedir(dp);
        }
    }

    *matches_out = matches;
    *count_out = count;
}

static int read_timeout(int fd, char *c, int timeout_ms)
{
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = timeout_ms * 1000;
    
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    
    int ret = select(fd + 1, &fds, NULL, NULL, &tv);
    if (ret > 0) {
        return (int)read(fd, c, 1);
    }
    return 0;
}

static char *read_line_interactive(char *buf, size_t size)
{
    struct termios orig_termios;
    tcgetattr(STDIN_FILENO, &orig_termios);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    size_t len = 0;
    buf[0] = '\0';
    int tab_count = 0;
    int history_browse_idx = -1;

    while (len < size - 1) {
        char c;
        int r = (int)read(STDIN_FILENO, &c, 1);
        if (r <= 0) {
            break;
        }

        if (c == '\033') { /* Escape sequence */
            char seq[2];
            if (read_timeout(STDIN_FILENO, &seq[0], 50) > 0 && 
                read_timeout(STDIN_FILENO, &seq[1], 50) > 0) {
                if (seq[0] == '[') {
                    if (seq[1] == 'A') { /* Up Arrow */
                        if (g_history_count > 0) {
                            if (history_browse_idx < 0) {
                                history_browse_idx = g_history_count - 1;
                            } else if (history_browse_idx > 0) {
                                history_browse_idx--;
                            }
                            
                            /* Clear current screen line */
                            while (len > 0) {
                                write(STDOUT_FILENO, "\b \b", 3);
                                len--;
                            }
                            strncpy(buf, g_history[history_browse_idx], size - 1);
                            len = strlen(buf);
                            write(STDOUT_FILENO, buf, len);
                        }
                    } else if (seq[1] == 'B') { /* Down Arrow */
                        if (history_browse_idx >= 0) {
                            history_browse_idx++;
                            if (history_browse_idx >= g_history_count) {
                                history_browse_idx = -1;
                                while (len > 0) {
                                    write(STDOUT_FILENO, "\b \b", 3);
                                    len--;
                                }
                                buf[0] = '\0';
                            } else {
                                while (len > 0) {
                                    write(STDOUT_FILENO, "\b \b", 3);
                                    len--;
                                }
                                strncpy(buf, g_history[history_browse_idx], size - 1);
                                len = strlen(buf);
                                write(STDOUT_FILENO, buf, len);
                            }
                        }
                    }
                }
            }
            continue;
        }

        if (c == '\003') { /* Ctrl-C */
            write(STDOUT_FILENO, "^C\n", 3);
            buf[0] = '\0';
            len = 0;
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
            return buf;
        }

        if (c == '\004') { /* Ctrl-D */
            if (len == 0) {
                tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
                return NULL;
            }
            continue;
        }

        if (c == '\032') { /* Ctrl-Z */
            write(STDOUT_FILENO, "^Z\n", 3);
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
            kill(getpid(), SIGTSTP);
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
            botshell_print_prompt();
            write(STDOUT_FILENO, buf, len);
            continue;
        }

        if (c == '\n' || c == '\r') {
            buf[len] = '\0';
            write(STDOUT_FILENO, "\n", 1);
            break;
        }

        if (c == 127 || c == '\b') { /* Backspace */
            if (len > 0) {
                len--;
                buf[len] = '\0';
                write(STDOUT_FILENO, "\b \b", 3);
            }
            tab_count = 0;
            continue;
        }

        if (c == '\t') {
            tab_count++;
            
            size_t wstart = len;
            while (wstart > 0 && buf[wstart - 1] != ' ' && buf[wstart - 1] != '\t' && buf[wstart - 1] != '|') {
                wstart--;
            }
            
            char prefix[256];
            size_t prefix_len = len - wstart;
            if (prefix_len > 255) prefix_len = 255;
            memcpy(prefix, buf + wstart, prefix_len);
            prefix[prefix_len] = '\0';
            
            int is_cmd = 1;
            for (size_t i = 0; i < wstart; i++) {
                if (buf[i] != ' ' && buf[i] != '\t') {
                    is_cmd = 0;
                    break;
                }
            }
            if (wstart > 1 && buf[wstart - 1] == ' ' && buf[wstart - 2] == '|') {
                is_cmd = 1;
            }
            
            char **matches = NULL;
            int match_count = 0;
            find_completions(prefix, is_cmd, &matches, &match_count);
            
            if (match_count == 1) {
                char *match = matches[0];
                size_t suffix_start = prefix_len;
                size_t match_len = strlen(match);
                
                while (suffix_start < match_len && len < size - 2) {
                    buf[len++] = match[suffix_start++];
                    write(STDOUT_FILENO, &buf[len-1], 1);
                }
                
                if (match_len > 0 && match[match_len - 1] != '/' && len < size - 2) {
                    buf[len++] = ' ';
                    write(STDOUT_FILENO, " ", 1);
                }
                buf[len] = '\0';
            } else if (match_count > 1) {
                size_t common_len = prefix_len;
                while (1) {
                    char next_char = matches[0][common_len];
                    if (next_char == '\0') break;
                    
                    int all_match = 1;
                    for (int m = 1; m < match_count; m++) {
                        if (matches[m][common_len] != next_char) {
                            all_match = 0;
                            break;
                        }
                    }
                    if (!all_match) break;
                    common_len++;
                }
                
                if (common_len > prefix_len) {
                    while (prefix_len < common_len && len < size - 2) {
                        buf[len++] = matches[0][prefix_len++];
                        write(STDOUT_FILENO, &buf[len-1], 1);
                    }
                    buf[len] = '\0';
                } else if (tab_count >= 2) {
                    write(STDOUT_FILENO, "\n", 1);
                    for (int m = 0; m < match_count; m++) {
                        write(STDOUT_FILENO, matches[m], strlen(matches[m]));
                        write(STDOUT_FILENO, "  ", 2);
                    }
                    write(STDOUT_FILENO, "\n", 1);
                    
                    botshell_print_prompt();
                    write(STDOUT_FILENO, buf, len);
                }
            }
            
            for (int m = 0; m < match_count; m++) {
                free(matches[m]);
            }
            free(matches);
            
            continue;
        }

        if (c >= 32 && c <= 126) {
            buf[len++] = c;
            buf[len] = '\0';
            write(STDOUT_FILENO, &c, 1);
            tab_count = 0;
        }
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    return buf;
}

static char *read_line(char *buf, size_t size)
{
    if (!g_interactive) {
        return fgets(buf, (int)size, stdin);
    }
    return read_line_interactive(buf, size);
}

static int is_for_start(const char *line)
{
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    return strncmp(p, "for ", 4) == 0;
}

static int is_if_start(const char *line)
{
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    return strncmp(p, "if ", 3) == 0;
}

static int is_while_start(const char *line)
{
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    return strncmp(p, "while ", 6) == 0;
}

static int count_substring(const char *str, const char *sub)
{
    int count = 0;
    const char *tmp = str;
    int len = (int)strlen(sub);
    while ((tmp = strstr(tmp, sub))) {
        int start_ok = (tmp == str || isspace((unsigned char)*(tmp - 1)) || *(tmp - 1) == ';');
        int end_ok = (*(tmp + len) == '\0' || isspace((unsigned char)*(tmp + len)) || *(tmp + len) == ';');
        if (start_ok && end_ok) {
            count++;
        }
        tmp += len;
    }
    return count;
}

static int is_block_complete(const char *block)
{
    int for_count = count_substring(block, "for");
    int while_count = count_substring(block, "while");
    int done_count = count_substring(block, "done");
    int if_count = count_substring(block, "if");
    int fi_count = count_substring(block, "fi");
    
    return ((for_count + while_count) == done_count) && (if_count == fi_count);
}

static void split_block_into_lines(const char *block, char ***lines_out, int *line_count_out)
{
    char **lines = NULL;
    int count = 0;
    
    char *block_copy = strdup(block);
    char *line = block_copy;
    char *next_line;
    while (*line) {
        next_line = line;
        while (*next_line && *next_line != '\n') {
            next_line++;
        }
        char save = *next_line;
        *next_line = '\0';
        
        lines = realloc(lines, (size_t)(count + 1) * sizeof(char *));
        lines[count++] = strdup(line);
        
        if (save == '\0') {
            break;
        }
        line = next_line + 1;
    }
    free(block_copy);
    
    *lines_out = lines;
    *line_count_out = count;
}

static int run_for_loop(const char *block)
{
    char **lines = NULL;
    int line_cnt = 0;
    split_block_into_lines(block, &lines, &line_cnt);
    if (line_cnt == 0) return 0;
    
    char *first_line = lines[0];
    const char *p = first_line;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "for ", 4) != 0) {
        for (int i = 0; i < line_cnt; i++) free(lines[i]);
        free(lines);
        return 0;
    }
    p += 4;
    while (*p == ' ' || *p == '\t') p++;
    
    char var_name[256];
    int vi = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != ';') {
        if (vi < 255) var_name[vi++] = *p;
        p++;
    }
    var_name[vi] = '\0';
    
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "in", 2) != 0) {
        for (int i = 0; i < line_cnt; i++) free(lines[i]);
        free(lines);
        return 0;
    }
    p += 2;
    while (*p == ' ' || *p == '\t') p++;
    
    char values_str[1024];
    int val_i = 0;
    while (*p && *p != ';') {
        if (val_i < 1023) values_str[val_i++] = *p;
        p++;
    }
    values_str[val_i] = '\0';
    
    int body_start_idx = 1;
    if (body_start_idx < line_cnt) {
        char *line_trimmed = lines[body_start_idx];
        while (*line_trimmed == ' ' || *line_trimmed == '\t') line_trimmed++;
        if (strcmp(line_trimmed, "do") == 0) {
            body_start_idx++;
        }
    }
    
    /* The reconstructed body can never be longer than `block` itself
     * (it's a contiguous subset of block's lines with the same '\n'
     * separators restored), so sizing from strlen(block) is always a
     * safe upper bound. This used to be a fixed `char body[4096]`
     * filled via unchecked strcat — any for-loop body whose lines
     * totaled more than 4KB silently overran the stack buffer. */
    size_t body_cap = strlen(block) + 1;
    char *body = calloc(1, body_cap);
    if (!body) {
        for (int i = 0; i < line_cnt; i++) free(lines[i]);
        free(lines);
        return 1;
    }
    for (int i = body_start_idx; i < line_cnt; i++) {
        char *line_trimmed = lines[i];
        while (*line_trimmed == ' ' || *line_trimmed == '\t') line_trimmed++;
        if (strcmp(line_trimmed, "done") == 0) {
            break;
        }
        strcat(body, lines[i]);
        strcat(body, "\n");
    }
    
    for (int i = 0; i < line_cnt; i++) free(lines[i]);
    free(lines);
    
    char *vals[256];
    int val_cnt = 0;
    char values_copy[1024];
    strcpy(values_copy, values_str);
    char *val_token = strtok(values_copy, " \t\r\n");
    while (val_token && val_cnt < 256) {
        vals[val_cnt++] = strdup(val_token);
        val_token = strtok(NULL, " \t\r\n");
    }
    
    char *body_copy = malloc(body_cap);
    if (!body_copy) {
        for (int i = 0; i < val_cnt; i++) free(vals[i]);
        free(body);
        return 1;
    }

    int last_exit = 0;
    for (int i = 0; i < val_cnt; i++) {
        setenv(var_name, vals[i], 1);
        
        strcpy(body_copy, body);
        
        char *line = body_copy;
        char *next_line;
        while (*line) {
            next_line = line;
            while (*next_line && *next_line != '\n' && *next_line != ';') {
                next_line++;
            }
            char save = *next_line;
            *next_line = '\0';
            
            if (!parser_is_empty(line)) {
                last_exit = botshell_exec_string(line);
            }
            
            if (save == '\0') {
                break;
            }
            line = next_line + 1;
        }
        
        free(vals[i]);
    }
    unsetenv(var_name);
    free(body_copy);
    free(body);
    return last_exit;
}

static int run_if_statement(const char *block)
{
    char **lines = NULL;
    int line_cnt = 0;
    split_block_into_lines(block, &lines, &line_cnt);
    if (line_cnt == 0) return 0;
    
    char *first_line = lines[0];
    const char *p = first_line;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "if ", 3) != 0) {
        for (int i = 0; i < line_cnt; i++) free(lines[i]);
        free(lines);
        return 0;
    }
    p += 3;
    
    char cond_str[1024];
    int cond_i = 0;
    while (*p && *p != ';') {
        if (cond_i < 1023) cond_str[cond_i++] = *p;
        p++;
    }
    cond_str[cond_i] = '\0';
    
    int body_start_idx = 1;
    if (body_start_idx < line_cnt) {
        char *line_trimmed = lines[body_start_idx];
        while (*line_trimmed == ' ' || *line_trimmed == '\t') line_trimmed++;
        if (strcmp(line_trimmed, "then") == 0) {
            body_start_idx++;
        }
    }
    
    /* Sized from strlen(block): then_body and else_body are each a
     * contiguous subset of block's lines, so block's own length is
     * always a safe upper bound for both. This used to be two fixed
     * `char [4096]` buffers filled via unchecked strcat — an if/else
     * body whose lines totaled more than 4KB silently overran the
     * stack buffer. */
    size_t body_cap = strlen(block) + 1;
    char *then_body = calloc(1, body_cap);
    char *else_body = calloc(1, body_cap);
    if (!then_body || !else_body) {
        free(then_body);
        free(else_body);
        for (int i = 0; i < line_cnt; i++) free(lines[i]);
        free(lines);
        return 1;
    }
    int in_else = 0;
    
    for (int i = body_start_idx; i < line_cnt; i++) {
        char *line_trimmed = lines[i];
        while (*line_trimmed == ' ' || *line_trimmed == '\t') line_trimmed++;
        
        if (strcmp(line_trimmed, "else") == 0) {
            in_else = 1;
            continue;
        }
        if (strcmp(line_trimmed, "fi") == 0) {
            break;
        }
        
        if (in_else) {
            strcat(else_body, lines[i]);
            strcat(else_body, "\n");
        } else {
            strcat(then_body, lines[i]);
            strcat(then_body, "\n");
        }
    }
    
    for (int i = 0; i < line_cnt; i++) free(lines[i]);
    free(lines);
    
    int cond_res = botshell_exec_string(cond_str);
    char *body = (cond_res == 0) ? then_body : else_body;
    
    char *body_copy = malloc(body_cap);
    if (!body_copy) {
        free(then_body);
        free(else_body);
        return 1;
    }
    strcpy(body_copy, body);
    char *line = body_copy;
    char *next_line;
    int last_exit = 0;
    while (*line) {
        next_line = line;
        while (*next_line && *next_line != '\n' && *next_line != ';') {
            next_line++;
        }
        char save = *next_line;
        *next_line = '\0';
        
        if (!parser_is_empty(line)) {
            last_exit = botshell_exec_string(line);
        }
        
        if (save == '\0') {
            break;
        }
        line = next_line + 1;
    }
    
    free(body_copy);
    free(then_body);
    free(else_body);
    return last_exit;
}

static int run_while_loop(const char *block)
{
    char **lines = NULL;
    int line_cnt = 0;
    split_block_into_lines(block, &lines, &line_cnt);
    if (line_cnt == 0) return 0;
    
    char *first_line = lines[0];
    const char *p = first_line;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "while ", 6) != 0) {
        for (int i = 0; i < line_cnt; i++) free(lines[i]);
        free(lines);
        return 0;
    }
    p += 6;
    while (*p == ' ' || *p == '\t') p++;
    
    char cond_str[1024];
    int cond_i = 0;
    while (*p && *p != ';') {
        if (cond_i < 1023) cond_str[cond_i++] = *p;
        p++;
    }
    cond_str[cond_i] = '\0';
    
    int body_start_idx = 1;
    if (body_start_idx < line_cnt) {
        char *line_trimmed = lines[body_start_idx];
        while (*line_trimmed == ' ' || *line_trimmed == '\t') line_trimmed++;
        if (strcmp(line_trimmed, "do") == 0) {
            body_start_idx++;
        }
    }
    
    /* Sized from strlen(block), the same safe-upper-bound reasoning as
     * run_for_loop/run_if_statement. This used to be a fixed
     * `char body[4096]` filled via unchecked strcat — confirmed via an
     * instrumented test build that a 5170-byte while-body silently
     * overran it (strlen(body) read back as 5170 inside the nominal
     * 4096-byte array, with no crash — a real, silent stack corruption,
     * not just a theoretical one). */
    size_t body_cap = strlen(block) + 1;
    char *body = calloc(1, body_cap);
    if (!body) {
        for (int i = 0; i < line_cnt; i++) free(lines[i]);
        free(lines);
        return 1;
    }
    for (int i = body_start_idx; i < line_cnt; i++) {
        char *line_trimmed = lines[i];
        while (*line_trimmed == ' ' || *line_trimmed == '\t') line_trimmed++;
        if (strcmp(line_trimmed, "done") == 0) {
            break;
        }
        strcat(body, lines[i]);
        strcat(body, "\n");
    }
    
    for (int i = 0; i < line_cnt; i++) free(lines[i]);
    free(lines);
    
    char *body_copy = malloc(body_cap);
    if (!body_copy) {
        free(body);
        return 1;
    }

    int last_exit = 0;
    int iter = 0;
    const int max_iter = 1000;
    
    while (1) {
        int cond_res = botshell_exec_string(cond_str);
        if (cond_res != 0) {
            break;
        }
        
        iter++;
        if (iter > max_iter) {
            fprintf(stderr, "botshell: maximum loop iteration limit exceeded (%d)\n", max_iter);
            last_exit = 1;
            break;
        }
        
        strcpy(body_copy, body);
        
        char *line = body_copy;
        char *next_line;
        while (*line) {
            next_line = line;
            while (*next_line && *next_line != '\n' && *next_line != ';') {
                next_line++;
            }
            char save = *next_line;
            *next_line = '\0';
            
            if (!parser_is_empty(line)) {
                last_exit = botshell_exec_string(line);
            }
            
            if (save == '\0') {
                break;
            }
            line = next_line + 1;
        }
    }
    
    free(body_copy);
    free(body);
    return last_exit;
}

int botshell_run(void)
{
    char input[BOTSHELL_MAX_INPUT];

    /* Print welcome banner in interactive mode */
    if (g_interactive && g_shell_config.color_enabled) {
        printf("\n");
        printf("  \033[1;36m    ____          __                 _____\033[0m\n");
        printf("  \033[1;36m   / __ )____  __/ /_____  _____    / ___/\033[0m\n");
        printf("  \033[1;36m  / __  / __ \\/ __  / __ \\/ ___/    \\__ \\ \033[0m\n");
        printf("  \033[1;36m / /_/ / /_/ / /_/ / /_/ (__  )    ___/ / \033[0m\n");
        printf("  \033[1;36m/_____/\\____/\\__/_/\\____/____/    /____/  \033[0m\n");
        printf("\n");
        printf("  \033[1;33mWelcome to BotOS Core v%s — Interactive Command Line\033[0m\n", BOTSHELL_VERSION);
        printf("  Type \033[1;32m'help'\033[0m to list commands, or \033[1;32m'dashboard'\033[0m for live system metrics.\n");
        if (pybridge_is_available()) {
            printf("  \033[1;35mPyBridge Active:\033[0m prefix command with '!' to execute Python code.\n");
        }
        printf("\n");
    }

    while (g_running) {
        botshell_print_prompt();

        /* Read input line */
        if (read_line(input, sizeof(input)) == NULL) {
            if (errno == EINTR) {
                /* Interrupted by signal (SIGINT) — retry */
                clearerr(stdin);
                continue;
            }
            /* EOF (Ctrl+D) */
            if (g_interactive) {
                printf("\n");
            }
            break;
        }

        /* Strip trailing newline and carriage return */
        size_t len = strlen(input);
        while (len > 0 && (input[len - 1] == '\n' || input[len - 1] == '\r')) {
            input[--len] = '\0';
        }

        /* Skip empty lines */
        if (parser_is_empty(input)) {
            continue;
        }

        /* Accumulate multi-line block if it starts a block and is not complete */
        if (is_for_start(input) || is_if_start(input) || is_while_start(input)) {
            char block_buf[8192];
            if (strlen(input) >= sizeof(block_buf)) {
                fprintf(stderr, "botshell: input block too long\n");
                continue;
            }
            strcpy(block_buf, input);

            while (!is_block_complete(block_buf)) {
                char next_line[BOTSHELL_MAX_INPUT];
                if (g_interactive) {
                    printf("> ");
                    fflush(stdout);
                }
                if (read_line(next_line, sizeof(next_line)) == NULL) {
                    break;
                }

                size_t nl_len = strlen(next_line);
                while (nl_len > 0 && (next_line[nl_len - 1] == '\n' || next_line[nl_len - 1] == '\r')) {
                    next_line[--nl_len] = '\0';
                }

                if (strlen(block_buf) + strlen(next_line) + 2 >= sizeof(block_buf)) {
                    fprintf(stderr, "botshell: input block too long\n");
                    break;
                }
                strcat(block_buf, "\n");
                strcat(block_buf, next_line);
            }

            if (strlen(block_buf) < sizeof(input)) {
                strcpy(input, block_buf);
            } else {
                /* Execute the large block directly */
                history_add(block_buf);
                g_command_count++;
                g_last_exit = botshell_exec_string(block_buf);
                continue;
            }
        }

        /* Add to history */
        history_add(input);
        g_command_count++;

        /* Execute the command */
        g_last_exit = botshell_exec_string(input);
    }

    return g_last_exit;
}

static void process_heredocs(pipeline_t *pipeline, char *temp_files[], int *temp_file_count)
{
    *temp_file_count = 0;
    for (int c = 0; c < pipeline->cmd_count; c++) {
        parsed_cmd_t *cmd = &pipeline->commands[c];
        if (cmd->redir_in.type == REDIR_HEREDOC && cmd->redir_in.filename) {
            char temp_path[] = "/tmp/botos-hdoc-XXXXXX";
            int fd = mkstemp(temp_path);
            if (fd < 0) {
                perror("botshell: mkstemp");
                continue;
            }
            
            char line[BOTSHELL_MAX_INPUT];
            char *delim = cmd->redir_in.filename;
            
            while (1) {
                if (g_interactive) {
                    printf("heredoc> ");
                    fflush(stdout);
                }
                if (!fgets(line, sizeof(line), stdin)) {
                    break;
                }
                
                size_t len = strlen(line);
                while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
                    line[--len] = '\0';
                }
                
                if (strcmp(line, delim) == 0) {
                    break;
                }
                
                write(fd, line, strlen(line));
                write(fd, "\n", 1);
            }
            close(fd);
            
            free(cmd->redir_in.filename);
            cmd->redir_in.filename = strdup(temp_path);
            cmd->redir_in.type = REDIR_IN;
            
            if (*temp_file_count < PARSER_MAX_PIPES) {
                temp_files[(*temp_file_count)++] = strdup(temp_path);
            }
        }
    }
}

int botshell_exec_string(const char *command)
{
    if (!command || parser_is_empty(command)) return 0;

    if (is_for_start(command)) {
        return run_for_loop(command);
    }
    if (is_if_start(command)) {
        return run_if_statement(command);
    }
    if (is_while_start(command)) {
        return run_while_loop(command);
    }

    /* Make a mutable copy for the parser */
    char *input = strdup(command);
    if (!input) return -1;

    /* Check for "exit" shortcut before parsing */
    {
        char *trimmed = input;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

        if (strncmp(trimmed, "exit", 4) == 0 &&
            (trimmed[4] == '\0' || trimmed[4] == ' ')) {
            g_running = 0;

            /* Parse optional exit code */
            int code = 0;
            if (trimmed[4] == ' ') {
                code = atoi(trimmed + 5);
            }
            free(input);
            return code;
        }
    }

    /* Check for PyBridge command (! prefix) */
    if (parser_is_pybridge(input) && g_shell_config.pybridge_enabled) {
        const char *ag_cmd = input;
        while (*ag_cmd == ' ') ag_cmd++;
        ag_cmd++;  /* Skip '!' */
        while (*ag_cmd == ' ') ag_cmd++;

        int ret = pybridge_execute(ag_cmd);
        free(input);
        return ret;
    }

    /* Parse into pipeline */
    pipeline_t pipeline;
    if (parser_parse_line(input, &pipeline) != 0) {
        free(input);
        return 0;  /* Empty after comment strip, not an error */
    }

    /* Expand glob wildcards */
    parser_expand_pipeline_globs(&pipeline);

    /* Process heredocs */
    char *temp_files[PARSER_MAX_PIPES];
    int temp_file_count = 0;
    process_heredocs(&pipeline, temp_files, &temp_file_count);

    /* Check for built-in commands (only single-command, no pipes) */
    if (pipeline.cmd_count == 1 &&
        pipeline.commands[0].argc > 0 &&
        builtin_is_builtin(pipeline.commands[0].argv[0])) {

        const parsed_cmd_t *cmd = &pipeline.commands[0];

        /* Save original fds so we can restore after builtin */
        int saved_stdin  = -1;
        int saved_stdout = -1;
        int saved_stderr = -1;
        int redir_ok = 1;

        /* Apply stdin redirection */
        if (cmd->redir_in.type == REDIR_IN && cmd->redir_in.filename) {
            int fd = open(cmd->redir_in.filename, O_RDONLY);
            if (fd < 0) {
                fprintf(stderr, "botshell: %s: %s\n",
                        cmd->redir_in.filename, strerror(errno));
                redir_ok = 0;
            } else {
                saved_stdin = dup(STDIN_FILENO);
                dup2(fd, STDIN_FILENO);
                close(fd);
            }
        }

        /* Apply stdout redirection */
        if (redir_ok && cmd->redir_out.filename) {
            int flags = O_WRONLY | O_CREAT;
            if (cmd->redir_out.type == REDIR_APPEND)
                flags |= O_APPEND;
            else
                flags |= O_TRUNC;

            int fd = open(cmd->redir_out.filename, flags, 0644);
            if (fd < 0) {
                fprintf(stderr, "botshell: %s: %s\n",
                        cmd->redir_out.filename, strerror(errno));
                redir_ok = 0;
            } else {
                saved_stdout = dup(STDOUT_FILENO);
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }
        }

        /* Apply stderr redirection */
        if (redir_ok && cmd->redir_err.type == REDIR_ERR && cmd->redir_err.filename) {
            int fd = open(cmd->redir_err.filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                fprintf(stderr, "botshell: %s: %s\n",
                        cmd->redir_err.filename, strerror(errno));
                redir_ok = 0;
            } else {
                saved_stderr = dup(STDERR_FILENO);
                dup2(fd, STDERR_FILENO);
                close(fd);
            }
        }

        /* Execute the builtin */
        int ret = redir_ok ? builtin_execute(cmd) : 1;

        /* Flush before restoring (critical — data must hit the redirected fd) */
        fflush(stdout);
        fflush(stderr);

        /* Restore original fds */
        if (saved_stdin >= 0)  { dup2(saved_stdin,  STDIN_FILENO);  close(saved_stdin);  }
        if (saved_stdout >= 0) { dup2(saved_stdout, STDOUT_FILENO); close(saved_stdout); }
        if (saved_stderr >= 0) { dup2(saved_stderr, STDERR_FILENO); close(saved_stderr); }

        /* Clean up heredoc temp files */
        for (int i = 0; i < temp_file_count; i++) {
            unlink(temp_files[i]);
            free(temp_files[i]);
        }

        parser_free_pipeline(&pipeline);
        free(input);
        return ret;
    }

    /* Execute external pipeline */
    exec_result_t result;
    memset(&result, 0, sizeof(result));
    int ret = executor_run_pipeline(&pipeline, &result);

    /* Clean up heredoc temp files */
    for (int i = 0; i < temp_file_count; i++) {
        unlink(temp_files[i]);
        free(temp_files[i]);
    }

    parser_free_pipeline(&pipeline);
    free(input);

    return ret;
}

int botshell_exec_script(const char *path)
{
    if (!path) return -1;

    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "botshell: cannot open script: %s: %s\n",
                path, strerror(errno));
        return -1;
    }

    char line[BOTSHELL_MAX_INPUT];
    int ret = 0;
    int line_num = 0;

    while (fgets(line, sizeof(line), fp)) {
        line_num++;

        /* Strip newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        /* Skip comments and empty lines */
        char *trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

        if (*trimmed == '\0' || *trimmed == '#') {
            continue;
        }

        /* Check for multi-line block start */
        char block_buf[8192];
        if (is_for_start(line) || is_if_start(line) || is_while_start(line)) {
            strcpy(block_buf, line);
            while (!is_block_complete(block_buf)) {
                char next_line[BOTSHELL_MAX_INPUT];
                if (fgets(next_line, sizeof(next_line), fp) == NULL) {
                    break;
                }
                line_num++;
                
                size_t nl_len = strlen(next_line);
                while (nl_len > 0 && (next_line[nl_len - 1] == '\n' || next_line[nl_len - 1] == '\r')) {
                    next_line[--nl_len] = '\0';
                }
                
                if (strlen(block_buf) + strlen(next_line) + 2 >= sizeof(block_buf)) {
                    fprintf(stderr, "botshell: script block too long\n");
                    break;
                }
                strcat(block_buf, "\n");
                strcat(block_buf, next_line);
            }
            trimmed = block_buf;
        }

        /* Execute the line/block */
        ret = botshell_exec_string(trimmed);

        /* Stop on fatal error in strict mode */
        if (ret != 0 && g_shell_config.verbose) {
            fprintf(stderr, "botshell: script %s: line %d failed (exit %d)\n",
                    path, line_num, ret);
        }
    }

    fclose(fp);
    return ret;
}

void botshell_shutdown(void)
{
    g_running = 0;

    /* Save history */
    history_save();

    /* Free history memory */
    history_free();

    /* Shutdown subsystems */
    executor_cleanup();

    if (g_shell_config.pybridge_enabled) {
        pybridge_shutdown();
    }
}

/* ════════════════════════════════════════════════════════════
 *  MAIN — BotShell Entry Point
 * ════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[])
{
    /* Parse command-line arguments */
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            /* Non-interactive: execute command string */
            botshell_init(NULL);
            int ret = botshell_exec_string(argv[i + 1]);
            botshell_shutdown();
            return ret;
        }

        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("BotOS Shell v%s\n", BOTSHELL_VERSION);
            return 0;
        }

        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: botshell [OPTIONS] [SCRIPT]\n");
            printf("\n");
            printf("Options:\n");
            printf("  -c COMMAND    Execute COMMAND and exit\n");
            printf("  -v, --version Show version\n");
            printf("  -h, --help    Show this help\n");
            printf("\n");
            printf("If SCRIPT is provided, execute it and exit.\n");
            printf("Otherwise, start interactive REPL.\n");
            return 0;
        }

        /* Assume it's a script file */
        botshell_init(NULL);
        int ret = botshell_exec_script(argv[i]);
        botshell_shutdown();
        return ret;
    }

    /* Interactive REPL mode */
    botshell_init(NULL);
    int ret = botshell_run();
    botshell_shutdown();

    return ret;
}
