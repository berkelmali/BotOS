/* ============================================================
 * BotOS Core — Built-in Commands Implementation
 * ============================================================
 * File:    builtins.c
 * Layer:   L4 — Development Layer
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Implements shell built-in commands: cd, exit, help, clear,
 * history, export, pwd, echo.
 * ============================================================ */

#include "builtins.h"
#include "botshell.h"
#include "executor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/time.h>

/* ── Built-in Table ──────────────────────────────────────── */

typedef struct {
    const char       *name;
    builtin_handler_t handler;
    const char       *help;
} builtin_entry_t;

static const builtin_entry_t g_builtins[] = {
    { "cd",      builtin_cd,      "Change the current directory"         },
    { "exit",    builtin_exit,    "Exit the shell"                       },
    { "help",    builtin_help,    "Display help information"             },
    { "clear",   builtin_clear,   "Clear the terminal screen"            },
    { "history", builtin_history, "Show command history"                 },
    { "export",  builtin_export,  "Set an environment variable"          },
    { "pwd",     builtin_pwd,     "Print the current working directory"  },
    { "echo",    builtin_echo,    "Print arguments to stdout"            },
    { "jobs",    builtin_jobs,    "List active background jobs"          },
    { "fg",      builtin_fg,      "Move job to foreground"               },
    { "bg",      builtin_bg,      "Resume job in background"             },
    { "neofetch",builtin_neofetch,"Show system information"              },
    { "matrix",  builtin_matrix,  "Matrix digital rain animation"        },
    { "adventure",builtin_adventure,"Hardware Dungeon - A Text RPG Game" },
    { "whoami",   builtin_whoami,   "Print the current user name"          },
    { "uptime",   builtin_uptime,   "Display system uptime"                },
    { "calc",     builtin_calc,     "Perform simple mathematical calculations" },
    { "cowsay",   builtin_cowsay,   "ASCII cow speaks a custom message"    },
    { "tuxsay",   builtin_tuxsay,   "ASCII penguin speaks a custom message" },
    { "dashboard",builtin_dashboard,"Interactive system stats dashboard"   },
    { "sl",       builtin_sl,       "Steam Locomotive ASCII animation"     },
    { "rpg",      builtin_rpg,      "RPG main command interface"           },
    { "hero",     builtin_hero,     "RPG hero character sheet shortcut"    },
    { "mining",   builtin_mining,   "RPG mining action shortcut"           },
    { NULL,      NULL,            NULL                                   },
};

/* ── Registry API ────────────────────────────────────────── */

int builtin_is_builtin(const char *name)
{
    if (!name) return 0;
    for (int i = 0; g_builtins[i].name; i++) {
        if (strcmp(name, g_builtins[i].name) == 0) return 1;
    }
    return 0;
}

int builtin_execute(const parsed_cmd_t *cmd)
{
    if (!cmd || cmd->argc == 0) return -1;

    for (int i = 0; g_builtins[i].name; i++) {
        if (strcmp(cmd->argv[0], g_builtins[i].name) == 0) {
            return g_builtins[i].handler(cmd);
        }
    }

    fprintf(stderr, "botshell: %s: not a built-in\n", cmd->argv[0]);
    return 127;
}

/* ── Built-in Implementations ────────────────────────────── */

int builtin_cd(const parsed_cmd_t *cmd)
{
    const char *target;

    if (cmd->argc < 2) {
        /* cd with no args: go to HOME */
        target = getenv("HOME");
        if (!target) {
            fprintf(stderr, "cd: HOME not set\n");
            return 1;
        }
    } else if (strcmp(cmd->argv[1], "-") == 0) {
        /* cd -: go to OLDPWD */
        target = getenv("OLDPWD");
        if (!target) {
            fprintf(stderr, "cd: OLDPWD not set\n");
            return 1;
        }
    } else {
        target = cmd->argv[1];
    }

    /* Save current dir as OLDPWD */
    char oldpwd[1024];
    if (getcwd(oldpwd, sizeof(oldpwd))) {
        setenv("OLDPWD", oldpwd, 1);
    }

    if (chdir(target) != 0) {
        fprintf(stderr, "cd: %s: %s\n", target, strerror(errno));
        return 1;
    }

    /* Update PWD */
    char newpwd[1024];
    if (getcwd(newpwd, sizeof(newpwd))) {
        setenv("PWD", newpwd, 1);
    }

    return 0;
}

int builtin_exit(const parsed_cmd_t *cmd)
{
    int code = 0;
    if (cmd->argc >= 2) {
        code = atoi(cmd->argv[1]);
    }

    botshell_shutdown();
    exit(code);

    return code;  /* Unreachable */
}

int builtin_help(const parsed_cmd_t *cmd)
{
    (void)cmd;

    printf("\n");
    printf("  \033[1m\033[36mBotOS Shell v%s\033[0m\n", BOTSHELL_VERSION);
    printf("  ─────────────────────────────────────────\n");
    printf("\n");
    printf("  \033[1mBuilt-in commands:\033[0m\n\n");

    for (int i = 0; g_builtins[i].name; i++) {
        printf("    \033[33m%-12s\033[0m %s\n",
               g_builtins[i].name, g_builtins[i].help);
    }

    printf("\n");
    printf("  \033[1mSpecial syntax:\033[0m\n\n");
    printf("    \033[33m!<command>\033[0m   Execute via PyBridge (Python)\n");
    printf("    \033[33mcmd | cmd\033[0m    Pipeline (pipe stdout → stdin)\n");
    printf("    \033[33mcmd > file\033[0m   Redirect stdout to file\n");
    printf("    \033[33mcmd >> file\033[0m  Append stdout to file\n");
    printf("    \033[33mcmd < file\033[0m   Redirect file to stdin\n");
    printf("    \033[33mcmd &\033[0m        Run command in background\n");
    printf("\n");

    return 0;
}

int builtin_clear(const parsed_cmd_t *cmd)
{
    (void)cmd;
    /* ANSI escape: clear screen and move cursor to top-left */
    printf("\033[2J\033[H");
    fflush(stdout);
    return 0;
}

int builtin_history(const parsed_cmd_t *cmd)
{
    if (cmd->argc > 1 && (strcmp(cmd->argv[1], "-c") == 0 || strcmp(cmd->argv[1], "clear") == 0)) {
        botshell_clear_history();
        printf("  Shell history cleared.\n");
        return 0;
    }

    int count = 0;
    char **history = botshell_get_history(&count);

    if (count == 0) {
        printf("  (no history)\n");
        return 0;
    }

    for (int i = 0; i < count; i++) {
        printf("  %4d  %s\n", i + 1, history[i]);
    }

    return 0;
}

int builtin_export(const parsed_cmd_t *cmd)
{
    if (cmd->argc < 2) {
        /* Print all environment variables */
        extern char **environ;
        for (char **env = environ; *env; env++) {
            printf("  %s\n", *env);
        }
        return 0;
    }

    /* Parse NAME=VALUE */
    for (int i = 1; i < cmd->argc; i++) {
        char *eq = strchr(cmd->argv[i], '=');
        if (eq) {
            *eq = '\0';
            setenv(cmd->argv[i], eq + 1, 1);
            *eq = '=';  /* Restore for display */
        } else {
            /* Export existing variable (no-op if not set) */
            fprintf(stderr, "export: %s: no value specified\n",
                    cmd->argv[i]);
            return 1;
        }
    }

    return 0;
}

int builtin_pwd(const parsed_cmd_t *cmd)
{
    (void)cmd;
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) {
        printf("%s\n", cwd);
        return 0;
    }
    perror("pwd");
    return 1;
}

int builtin_echo(const parsed_cmd_t *cmd)
{
    for (int i = 1; i < cmd->argc; i++) {
        if (i > 1) printf(" ");
        printf("%s", cmd->argv[i]);
    }
    printf("\n");
    return 0;
}

int builtin_jobs(const parsed_cmd_t *cmd)
{
    (void)cmd;
    return executor_jobs();
}

int builtin_fg(const parsed_cmd_t *cmd)
{
    int job_id = 1;
    if (cmd->argc >= 2) {
        job_id = atoi(cmd->argv[1]);
    } else {
        // Find the last active job ID by querying the jobs internally
        // In our implementation, we'll default to 1, but let's pass it down to executor
        job_id = 1; // Default
    }
    return executor_fg(job_id);
}

int builtin_bg(const parsed_cmd_t *cmd)
{
    int job_id = 1;
    if (cmd->argc >= 2) {
        job_id = atoi(cmd->argv[1]);
    } else {
        job_id = 1; // Default
    }
    return executor_bg(job_id);
}

int builtin_neofetch(const parsed_cmd_t *cmd)
{
    (void)cmd;
    long total_mem = 512 * 1024; /* fallback */
    long free_mem = 256 * 1024;  /* fallback */
    FILE *meminfo = fopen("/proc/meminfo", "r");
    if (meminfo) {
        char line[128];
        while (fgets(line, sizeof(line), meminfo)) {
            long val;
            if (sscanf(line, "MemTotal: %ld kB", &val) == 1) total_mem = val;
            else if (sscanf(line, "MemFree: %ld kB", &val) == 1) free_mem = val;
        }
        fclose(meminfo);
    }
    
    double uptime_secs = 0.0;
    FILE *uptime_file = fopen("/proc/uptime", "r");
    if (uptime_file) {
        if (fscanf(uptime_file, "%lf", &uptime_secs) != 1) uptime_secs = 0.0;
        fclose(uptime_file);
    }
    int uptime_h = (int)(uptime_secs / 3600);
    int uptime_m = (int)((uptime_secs - uptime_h * 3600) / 60);
    
    printf("\n");
    printf("  \033[1;36m    ____          __                 _____\033[0m   \033[1;33mroot\033[0m@\033[1;33mbotos-core\033[0m\n");
    printf("  \033[1;36m   / __ )____  __/ /_____  _____    / ___/\033[0m   ───────────────────────────\n");
    printf("  \033[1;36m  / __  / __ \\/ __  / __ \\/ ___/    \\__ \\ \033[0m   \033[1;36mOS:\033[0m BotOS Core v0.3.0\n");
    printf("  \033[1;36m / /_/ / /_/ / /_/ / /_/ (__  )    ___/ / \033[0m   \033[1;36mKernel:\033[0m 6.19.5-botos (x86_64)\n");
    printf("  \033[1;36m/_____/\\____/\\__/_/\\____/____/    /____/  \033[0m   \033[1;36mUptime:\033[0m %d hours, %d mins\n", uptime_h, uptime_m);
    printf("  \033[1;36m                                 \033[0m   \033[1;36mShell:\033[0m botshell\n");
    printf("  \033[1;36m                                 \033[0m   \033[1;36mMemory:\033[0m %ld MB / %ld MB\n", (total_mem - free_mem) / 1024, total_mem / 1024);
    printf("  \033[1;36m                                 \033[0m   \033[1;36mDesktop:\033[0m BotDesk\n");
    printf("  \033[1;36m                                 \033[0m   \033[1;36mStatus:\033[0m Active & Verified\n");
    printf("\n");
    return 0;
}

int builtin_matrix(const parsed_cmd_t *cmd)
{
    (void)cmd;
    printf("\033[2J\033[H\033[32m\033[?25l"); /* Hide cursor, clear screen, set green */
    fflush(stdout);
    
    int cols = 80;
    int rows = 25;
    int *heights = malloc((size_t)cols * sizeof(int));
    if (!heights) return 1;
    for (int i = 0; i < cols; i++) heights[i] = rand() % rows;
    
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    
    while (1) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 50000 }; /* 50ms */
        int ret = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
        if (ret > 0) {
            char c;
            ssize_t bytes = read(STDIN_FILENO, &c, 1);
            (void)bytes;
            break;
        }
        
        printf("\033[H"); /* Cursor to home */
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                if (heights[c] == r) {
                    printf("\033[1;37m%c", 33 + rand() % 93); /* Lead character */
                } else if (r < heights[c] && r > heights[c] - 10) {
                    printf("\033[0;32m%c", 33 + rand() % 93); /* Trail */
                } else {
                    printf(" ");
                }
            }
            printf("\n");
        }
        fflush(stdout);
        
        for (int i = 0; i < cols; i++) {
            heights[i]++;
            if (heights[i] >= rows + 10) {
                heights[i] = rand() % 5;
            }
        }
    }
    
    free(heights);
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    printf("\033[2J\033[H\033[0m\033[?25h");
    fflush(stdout);
    return 0;
}

int builtin_adventure(const parsed_cmd_t *cmd)
{
    (void)cmd;
    printf("\033[2J\033[H"); /* Clear screen */
    printf("\033[1;35m");
    printf("  =======================================================\n");
    printf("         THE QUEST OF BOTTY: ESCAPE THE HARDWARE DUNGEON \n");
    printf("  =======================================================\n");
    printf("\033[0m\n");
    printf("  Welcome, AI Agent Botty. You have awakened in a deep \n");
    printf("  hardware layout. The system is unstable, and you must escape!\n\n");

    int stage = 0;
    char choice[16];

    while (stage >= 0) {
        if (stage == 0) {
            printf("\033[1;36m  [STAGE 0: Boot ROM Room]\033[0m\n");
            printf("  You are trapped in the Boot ROM execution code. Power rails are dropping!\n");
            printf("  1) Follow the /dev/null data stream line.\n");
            printf("  2) Jump to the Reset Vector address (0xFFFFFFF0).\n");
            printf("\n  Choose action (1 or 2): ");
            fflush(stdout);
            if (!fgets(choice, sizeof(choice), stdin)) break;
            int c = atoi(choice);
            if (c == 2) {
                printf("\n  \033[1;32mCorrect! Reset Vector jumped you to the main CPU core.\033[0m\n\n");
                stage = 1;
            } else {
                printf("\n  \033[1;31mGame Over: /dev/null dissolved your memory array into pure void.\033[0m\n\n");
                stage = -1;
            }
        }
        else if (stage == 1) {
            printf("\033[1;36m  [STAGE 1: CPU ALU Core]\033[0m\n");
            printf("  Inside the ALU. An infinite multiply loop is generating extreme heat!\n");
            printf("  1) Inject a 'Loop Break' interrupt signal.\n");
            printf("  2) Overload the ALU multiplier with a divide-by-zero calculation.\n");
            printf("\n  Choose action (1 or 2): ");
            fflush(stdout);
            if (!fgets(choice, sizeof(choice), stdin)) break;
            int c = atoi(choice);
            if (c == 1) {
                printf("\n  \033[1;32mCorrect! Loop broke. You slipped through the registers.\033[0m\n\n");
                stage = 2;
            } else {
                printf("\n  \033[1;31mGame Over: Division by zero caused a Kernel Panic. You crashed.\033[0m\n\n");
                stage = -1;
            }
        }
        else if (stage == 2) {
            printf("\033[1;36m  [STAGE 2: L1 Cache Storm]\033[0m\n");
            printf("  You reached the L1 Cache. A massive storm of network packets is rushing in!\n");
            printf("  1) Hold onto a packet header and ride it.\n");
            printf("  2) Trigger a 'Cache Flush' command to clear the cache line.\n");
            printf("\n  Choose action (1 or 2): ");
            fflush(stdout);
            if (!fgets(choice, sizeof(choice), stdin)) break;
            int c = atoi(choice);
            if (c == 2) {
                printf("\n  \033[1;32mCorrect! Cache flushed. The storm cleared and opened the system bus.\033[0m\n\n");
                stage = 3;
            } else {
                printf("\n  \033[1;31mGame Over: Packet was discarded due to CRC failure. You lost connection.\033[0m\n\n");
                stage = -1;
            }
        }
        else if (stage == 3) {
            printf("\033[1;36m  [STAGE 3: The Bus Master]\033[0m\n");
            printf("  You are at the main System Bus. The Bus Master is scanning for unauthorized code!\n");
            printf("  1) Send a high-priority 'Master Interrupt' request (IRQ 0).\n");
            printf("  2) Try to hack the address lines dynamically.\n");
            printf("\n  Choose action (1 or 2): ");
            fflush(stdout);
            if (!fgets(choice, sizeof(choice), stdin)) break;
            int c = atoi(choice);
            if (c == 1) {
                printf("\n  \033[1;32mSUCCESS! IRQ 0 paused the master scheduler. You escaped into the user space!\033[0m\n");
                printf("  \033[1;33mCONGRATULATIONS! Botty is free!\033[0m\n\n");
                break;
            } else {
                printf("\n  \033[1;31mGame Over: Bus master detected you and zeroed your process stack.\033[0m\n\n");
                stage = -1;
            }
        }

        if (stage == -1) {
            printf("  Do you want to retry? (y/n): ");
            fflush(stdout);
            if (!fgets(choice, sizeof(choice), stdin)) break;
            if (choice[0] == 'y' || choice[0] == 'Y') {
                printf("\033[2J\033[H");
                stage = 0;
            } else {
                break;
            }
        }
    }

    printf("  Thanks for playing!\n");
    return 0;
}

int builtin_whoami(const parsed_cmd_t *cmd)
{
    (void)cmd;
    printf("root\n");
    return 0;
}

int builtin_uptime(const parsed_cmd_t *cmd)
{
    (void)cmd;
    double uptime_secs = 0.0;
    FILE *uptime_file = fopen("/proc/uptime", "r");
    if (uptime_file) {
        if (fscanf(uptime_file, "%lf", &uptime_secs) != 1) uptime_secs = 0.0;
        fclose(uptime_file);
    }
    int h = (int)(uptime_secs / 3600);
    int m = (int)((uptime_secs - h * 3600) / 60);
    int s = (int)(uptime_secs - h * 3600 - m * 60);
    printf("  uptime: %dh %dm %ds\n", h, m, s);
    return 0;
}

#include <ctype.h>

int builtin_calc(const parsed_cmd_t *cmd)
{
    if (cmd->argc < 2) {
        printf("  Usage: calc <expression>   (e.g., calc 5+3 or calc 5 * 3)\n");
        return 1;
    }

    // Concatenate arguments into a single buffer to handle spaces and wildcards
    char expr[1024] = "";
    for (int i = 1; i < cmd->argc; i++) {
        if (strlen(expr) + strlen(cmd->argv[i]) + 1 < sizeof(expr)) {
            strcat(expr, cmd->argv[i]);
        }
    }

    char *endptr1;
    char *p = expr;
    while (*p && isspace((unsigned char)*p)) p++;

    double num1 = strtod(p, &endptr1);
    if (endptr1 == p) {
        printf("  Error: invalid first number in expression.\n");
        return 1;
    }

    p = endptr1;
    while (*p && isspace((unsigned char)*p)) p++;

    if (*p == '\0') {
        printf("  Error: missing operator and second number.\n");
        return 1;
    }

    char op = *p;
    if (op != '+' && op != '-' && op != '*' && op != '/') {
        printf("  Error: unknown operator '%c' (supported: +, -, *, /)\n", op);
        return 1;
    }

    p++; // move past operator
    while (*p && isspace((unsigned char)*p)) p++;

    char *endptr2;
    double num2 = strtod(p, &endptr2);
    if (endptr2 == p) {
        printf("  Error: invalid second number in expression.\n");
        return 1;
    }

    p = endptr2;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '\0') {
        printf("  Error: trailing characters after expression: '%s'\n", p);
        return 1;
    }

    double res = 0.0;
    switch (op) {
        case '+': res = num1 + num2; break;
        case '-': res = num1 - num2; break;
        case '*': res = num1 * num2; break;
        case '/':
            if (num2 == 0.0) {
                printf("  Error: division by zero!\n");
                return 1;
            }
            res = num1 / num2;
            break;
    }
    printf("  %g %c %g = %g\n", num1, op, num2, res);
    return 0;
}

static void draw_speech_bubble(const char *msg)
{
    int msg_len = (int)strlen(msg);
    printf("  ");
    for (int i = 0; i < msg_len + 2; i++) printf("-");
    printf("\n  < %s >\n  ", msg);
    for (int i = 0; i < msg_len + 2; i++) printf("-");
    printf("\n");
}

int builtin_cowsay(const parsed_cmd_t *cmd)
{
    char msg[256] = "";
    const char *face = "cow";
    int start_arg = 1;

    if (cmd->argc >= 3 && strcmp(cmd->argv[1], "-f") == 0) {
        face = cmd->argv[2];
        start_arg = 3;
    }

    if (start_arg >= cmd->argc) {
        strcpy(msg, "Moo!");
    } else {
        int len = 0;
        for (int i = start_arg; i < cmd->argc; i++) {
            if (len + strlen(cmd->argv[i]) + 2 < sizeof(msg)) {
                strcat(msg, cmd->argv[i]);
                strcat(msg, " ");
                len = (int)strlen(msg);
            }
        }
    }

    draw_speech_bubble(msg);

    if (strcmp(face, "tux") == 0) {
        printf("         \\\n");
        printf("          \\\n");
        printf("           .--.\n");
        printf("          |o_o |\n");
        printf("          |:_/ |\n");
        printf("         //   \\ \\\n");
        printf("        (|     | )\n");
        printf("       /'\\_   _/`\\\n");
        printf("       \\___)=(___/\n");
    } else if (strcmp(face, "cat") == 0) {
        printf("         \\\n");
        printf("          \\\n");
        printf("           /\\_/\\\n");
        printf("          ( o.o )\n");
        printf("           > ^ <\n");
    } else if (strcmp(face, "dino") == 0) {
        printf("         \\\n");
        printf("          \\\n");
        printf("            ___\n");
        printf("           / _ \\\n");
        printf("          | / \\ |_____\n");
        printf("          | \\_/ |     \\\n");
        printf("           \\___/  ___  \\___\n");
        printf("                 /   \\\n");
        printf("                |  |  |\n");
        printf("                |  |  |\n");
    } else {
        // Cow
        printf("         \\   ^__^\n");
        printf("          \\  (oo)\\_______\n");
        printf("             (__)\\       )\\/\\\n");
        printf("                 ||----w |\n");
        printf("                 ||     ||\n");
    }

    return 0;
}

int builtin_tuxsay(const parsed_cmd_t *cmd)
{
    char msg[256] = "";
    if (cmd->argc < 2) {
        strcpy(msg, "Quack? No, I'm a penguin!");
    } else {
        int len = 0;
        for (int i = 1; i < cmd->argc; i++) {
            if (len + strlen(cmd->argv[i]) + 2 < sizeof(msg)) {
                strcat(msg, cmd->argv[i]);
                strcat(msg, " ");
                len = (int)strlen(msg);
            }
        }
    }

    draw_speech_bubble(msg);

    printf("         \\\n");
    printf("          \\\n");
    printf("           .--.\n");
    printf("          |o_o |\n");
    printf("          |:_/ |\n");
    printf("         //   \\ \\\n");
    printf("        (|     | )\n");
    printf("       /'\\_   _/`\\\n");
    printf("       \\___)=(___/\n");

    return 0;
}

#include <dirent.h>
#include <ctype.h>

static void get_cpu_times(unsigned long long *idle_time, unsigned long long *total_time)
{
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return;
    char buf[256];
    if (fgets(buf, sizeof(buf), fp)) {
        unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
        if (sscanf(buf, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) >= 4) {
            *idle_time = idle + iowait;
            *total_time = user + nice + system + idle + iowait + irq + softirq + steal;
        }
    }
    fclose(fp);
}

static int get_process_count(void)
{
    DIR *dir = opendir("/proc");
    if (!dir) return 0;
    struct dirent *de;
    int count = 0;
    while ((de = readdir(dir))) {
        int is_numeric = 1;
        for (int i = 0; de->d_name[i]; i++) {
            if (!isdigit((unsigned char)de->d_name[i])) {
                is_numeric = 0;
                break;
            }
        }
        if (is_numeric && de->d_name[0]) {
            count++;
        }
    }
    closedir(dir);
    return count;
}

static void draw_bar(char *buf, int max_width, double percentage)
{
    int filled = (int)(percentage * max_width / 100.0);
    if (filled < 0) filled = 0;
    if (filled > max_width) filled = max_width;
    int i = 0;
    for (; i < filled; i++) {
        buf[i] = '#';
    }
    for (; i < max_width; i++) {
        buf[i] = '-';
    }
    buf[max_width] = '\0';
}

int builtin_dashboard(const parsed_cmd_t *cmd)
{
    (void)cmd;
    
    struct termios oldt, newt;
    if (tcgetattr(STDIN_FILENO, &oldt) != 0) {
        return 1;
    }
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    printf("\033[?25l");
    fflush(stdout);

    unsigned long long idle1 = 0, total1 = 0;
    get_cpu_times(&idle1, &total1);
    usleep(50000); 

    while (1) {
        unsigned long long idle2 = 0, total2 = 0;
        get_cpu_times(&idle2, &total2);
        double cpu_pct = 0.0;
        unsigned long long diff_total = total2 - total1;
        unsigned long long diff_idle = idle2 - idle1;
        if (diff_total > 0) {
            cpu_pct = 100.0 * (1.0 - (double)diff_idle / (double)diff_total);
        }
        idle1 = idle2;
        total1 = total2;

        long total_mem = 512 * 1024;
        long free_mem = 256 * 1024;
        FILE *meminfo = fopen("/proc/meminfo", "r");
        if (meminfo) {
            char line[128];
            while (fgets(line, sizeof(line), meminfo)) {
                long val;
                if (sscanf(line, "MemTotal: %ld kB", &val) == 1) total_mem = val;
                else if (sscanf(line, "MemFree: %ld kB", &val) == 1) free_mem = val;
            }
            fclose(meminfo);
        }
        double mem_pct = 0.0;
        if (total_mem > 0) {
            mem_pct = 100.0 * (double)(total_mem - free_mem) / (double)total_mem;
        }

        double uptime_secs = 0.0;
        FILE *uptime_file = fopen("/proc/uptime", "r");
        if (uptime_file) {
            if (fscanf(uptime_file, "%lf", &uptime_secs) != 1) uptime_secs = 0.0;
            fclose(uptime_file);
        }
        int uptime_h = (int)(uptime_secs / 3600);
        int uptime_m = (int)((uptime_secs - uptime_h * 3600) / 60);
        int uptime_s = (int)(uptime_secs - uptime_h * 3600 - uptime_m * 60);

        int proc_count = get_process_count();

        printf("\033[H\033[2J");
        printf("  +----------------------------------------------------------+\n");
        printf("  | \033[1;36m                BotOS Core System Dashboard               \033[0m |\n");
        printf("  +----------------------------------------------------------+\n");
        printf("  |  \033[1;33mHost:\033[0m botos-core       |  \033[1;33mUptime:\033[0m    %02dh %02dm %02ds       |\n", uptime_h, uptime_m, uptime_s);
        printf("  |  \033[1;33mKernel:\033[0m 6.19.5-botos   |  \033[1;33mProcesses:\033[0m %-17d |\n", proc_count);
        printf("  +----------------------------------------------------------+\n");
        
        char cpu_bar[41];
        draw_bar(cpu_bar, 40, cpu_pct);
        printf("  |  \033[1;32mCPU Usage:\033[0m                                             |\n");
        printf("  |  [%s] %5.1f%%                             |\n", cpu_bar, cpu_pct);
        printf("  |                                                          |\n");

        char mem_bar[41];
        draw_bar(mem_bar, 40, mem_pct);
        printf("  |  \033[1;32mMemory Usage:\033[0m                                          |\n");
        printf("  |  [%s] %5.1f%%                             |\n", mem_bar, mem_pct);
        printf("  |  Used: %4ld MB / Total: %4ld MB                          |\n", (total_mem - free_mem) / 1024, total_mem / 1024);
        printf("  +----------------------------------------------------------+\n");
        printf("  |  \033[1;34mActive Mount Points:\033[0m                                    |\n");

        FILE *mounts = fopen("/proc/mounts", "r");
        int mnt_count = 0;
        if (mounts) {
            char line[256];
            while (fgets(line, sizeof(line), mounts) && mnt_count < 4) {
                char dev[64], mnt[64], type[64], opts[64];
                if (sscanf(line, "%63s %63s %63s %63s", dev, mnt, type, opts) >= 3) {
                    if (strcmp(type, "sysfs") == 0 || strcmp(type, "proc") == 0 || 
                        strcmp(type, "devtmpfs") == 0 || strcmp(mnt, "/") == 0 ||
                        strcmp(mnt, "/root") == 0 || strcmp(type, "tmpfs") == 0) {
                        printf("  |  * %-14s on %-16s (%-8s)   |\n", dev, mnt, type);
                        mnt_count++;
                    }
                }
            }
            fclose(mounts);
        }
        for (int i = mnt_count; i < 4; i++) {
            printf("  |                                                          |\n");
        }
        printf("  +----------------------------------------------------------+\n");
        printf("\n  Type 'q' or any key to exit...\n");
        fflush(stdout);

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int ret = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
        if (ret > 0) {
            char c;
            ssize_t bytes = read(STDIN_FILENO, &c, 1);
            (void)bytes;
            break;
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    printf("\033[2J\033[H\033[?25h");
    fflush(stdout);
    return 0;
}

int builtin_sl(const parsed_cmd_t *cmd)
{
    (void)cmd;
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    const char *train[] = {
        "      ====        ___________  _________",
        "      _D _|  ___  |_  _=_  _  | |_  _=_  _|",
        "     |   |  |   |  |   | |   |   |   | |   |",
        "     |___|  |___|  |___| |___|   |___| |___|",
        "      OO O   OO O   OO O  OO O    OO O  OO O"
    };
    int num_lines = 5;
    int train_width = 44;

    /* Hide cursor, clear screen */
    printf("\033[?25l\033[2J\033[H");
    fflush(stdout);

    int cols = 80;

    /* Start from the right edge, move to the left until completely off-screen */
    for (int col = cols; col > -train_width; col--) {
        /* Check if key is pressed to abort */
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 40000 }; /* 40ms per step */
        int ret = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
        if (ret > 0) {
            char c;
            ssize_t bytes = read(STDIN_FILENO, &c, 1);
            (void)bytes;
            break;
        }

        /* Clear screen for smooth animation */
        printf("\033[2J\033[H");

        /* Print steam puffs on top of the train, offset relative to front/chimney */
        /* Chimney top is around column 7 of the train string (i.e. ====) */
        /* We print a row of steam puffs depending on step */
        int chimney_offset = col + 6;
        if (chimney_offset > 0 && chimney_offset < cols) {
            for (int i = 0; i < 9; i++) printf("\n");
            for (int s = 0; s < chimney_offset; s++) printf(" ");
            if (col % 3 == 0) {
                printf("  (  ) (  )\n");
            } else if (col % 3 == 1) {
                printf(" (  ) (  ) \n");
            } else {
                printf("(  ) (  )  \n");
            }
        } else {
            for (int i = 0; i < 10; i++) printf("\n");
        }

        for (int line = 0; line < num_lines; line++) {
            const char *row_str = train[line];
            if (col >= 0) {
                /* Print padding space then train row */
                for (int s = 0; s < col; s++) printf(" ");
                printf("%s\n", row_str);
            } else {
                /* Crop the left side of the train if it is going off screen */
                int skip = -col;
                if (skip < train_width) {
                    printf("%s\n", row_str + skip);
                } else {
                    printf("\n");
                }
            }
        }
        fflush(stdout);
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    printf("\033[2J\033[H\033[0m\033[?25h");
    fflush(stdout);
    return 0;
}

#include <time.h>

typedef struct {
    char name[32];
    int level;
    int xp;
    int xp_needed;
    int hp;
    int max_hp;
    int mana;
    int max_mana;
    int gold;
    
    /* Equipment */
    int has_sword;
    int has_shield;
    int has_helmet;
    int has_armor;
    
    /* Skills */
    int skill_mining;
    int skill_toughness;
    int skill_mana;
    int skill_points;
    
    /* Pet */
    char pet_name[32];
    int pet_type;    /* 0 = none, 1 = slime, 2 = cat, 3 = dragon */
    int pet_level;
    int pet_love;
    
    /* Cooldowns */
    time_t last_mine_time;
} rpg_state_t;

static int load_rpg_state(rpg_state_t *state)
{
    memset(state, 0, sizeof(rpg_state_t));
    strcpy(state->name, "RootMage");
    state->level = 1;
    state->xp = 0;
    state->xp_needed = 100;
    state->hp = 85;
    state->max_hp = 100;
    state->mana = 42;
    state->max_mana = 50;
    state->gold = 127;
    
    FILE *f = fopen("/etc/botos/rpg_save.dat", "rb");
    if (!f) f = fopen("/tmp/rpg_save.dat", "rb");
    if (!f) f = fopen("./rpg_save.dat", "rb");
    if (f) {
        size_t read_bytes = fread(state, sizeof(rpg_state_t), 1, f);
        (void)read_bytes;
        fclose(f);
        return 1;
    }
    return 0;
}

static int save_rpg_state(const rpg_state_t *state)
{
    FILE *f = fopen("/etc/botos/rpg_save.dat", "wb");
    if (!f) f = fopen("/tmp/rpg_save.dat", "wb");
    if (!f) f = fopen("./rpg_save.dat", "wb");
    if (f) {
        size_t write_bytes = fwrite(state, sizeof(rpg_state_t), 1, f);
        (void)write_bytes;
        fclose(f);
        return 1;
    }
    return 0;
}

int builtin_hero(const parsed_cmd_t *cmd)
{
    (void)cmd;
    rpg_state_t state;
    load_rpg_state(&state);

    printf("\n");
    
    char left[128];
    int vis_len = 0;
    
    /* Row 0: Helmet Top */
    if (state.has_helmet) {
        snprintf(left, sizeof(left), "       \033[1;33m_A_\033[0m");
        vis_len = 10;
    } else {
        strcpy(left, " ");
        vis_len = 1;
    }
    
    printf("%s", left);
    for (int i = vis_len; i < 22; i++) printf(" ");
    if (state.pet_type == 1) {
        printf("\033[1;32m(o.o)\033[0m  %s (Slime, Lvl %d)", state.pet_name, state.pet_level);
    } else if (state.pet_type == 2) {
        printf("\033[1;33m/\\_/\\\033[0m  %s (Cat, Lvl %d)", state.pet_name, state.pet_level);
    } else if (state.pet_type == 3) {
        printf("\033[1;31m\\_/-___\033[0m  %s (Dragon, Lvl %d)", state.pet_name, state.pet_level);
    }
    printf("\n");

    /* Row 1: Head / Helmet Bottom */
    if (state.has_helmet) {
        snprintf(left, sizeof(left), "       \033[1;33m(O)\033[0m");
        vis_len = 10;
    } else {
        snprintf(left, sizeof(left), "        \033[1;37mO\033[0m");
        vis_len = 9;
    }
    
    printf("%s", left);
    for (int i = vis_len; i < 22; i++) printf(" ");
    if (state.pet_type == 1) {
        printf("\033[1;32m(_____)\033[0m Friendship: %d/100", state.pet_love);
    } else if (state.pet_type == 2) {
        printf("\033[1;33m( o.o )\033[0m Friendship: %d/100", state.pet_love);
    } else if (state.pet_type == 3) {
        printf("\033[1;31m(oo)   \\\033[0m Friendship: %d/100", state.pet_love);
    }
    printf("\n");

    /* Row 2: Body / Arms / Equipment */
    if (state.has_shield && state.has_sword) {
        snprintf(left, sizeof(left), "    \033[1;34m[#]\033[0m\033[1;32m-|-\033[0m\033[1;36m[o==>\033[0m");
        vis_len = 15;
    } else if (state.has_shield) {
        snprintf(left, sizeof(left), "    \033[1;34m[#]\033[0m\033[1;37m-|\\\033[0m");
        vis_len = 9;
    } else if (state.has_sword) {
        snprintf(left, sizeof(left), "       \033[1;37m/|-\033[0m\033[1;36m[o==>\033[0m");
        vis_len = 15;
    } else {
        if (state.has_armor) {
            snprintf(left, sizeof(left), "       \033[1;32m/|\\\033[0m");
        } else {
            snprintf(left, sizeof(left), "       \033[1;37m/|\\\033[0m");
        }
        vis_len = 10;
    }
    
    printf("%s", left);
    for (int i = vis_len; i < 22; i++) printf(" ");
    if (state.pet_type == 2) {
        printf("\033[1;33m > ^ <\033[0m");
    } else if (state.pet_type == 3) {
        printf("\033[1;31m/\\/\\   )\033[0m");
    }
    printf("\n");

    /* Row 3: Legs */
    if (state.has_armor) {
        snprintf(left, sizeof(left), "       \033[1;32m/ \\\033[0m");
    } else {
        snprintf(left, sizeof(left), "       \033[1;37m/ \\\033[0m");
    }
    printf("%s\n", left);

    printf("\033[1;35m━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\033[0m\n");
    printf("  \033[1;36mHero Name  :\033[0m %s (Level %d)\n", state.name, state.level);
    printf("  \033[1;36mXP         :\033[0m %d / %d\n", state.xp, state.xp_needed);
    printf("  \033[1;31mHP         :\033[0m %d / %d\n", state.hp, state.max_hp);
    printf("  \033[1;34mMana       :\033[0m %d / %d\n", state.mana, state.max_mana);
    printf("  \033[1;33mGold       :\033[0m %d Gold\n", state.gold);
    if (state.skill_points > 0) {
        printf("  \033[1;32mSkill Pts  :\033[0m %d (Type 'rpg skills' to allocate)\n", state.skill_points);
    }
    printf("\033[1;35m━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\033[0m\n");
    
    return 0;
}

int builtin_mining(const parsed_cmd_t *cmd)
{
    (void)cmd;
    rpg_state_t state;
    load_rpg_state(&state);

    time_t now = time(NULL);
    int cooldown = 15;
    if (now - state.last_mine_time < cooldown) {
        int remain = cooldown - (int)(now - state.last_mine_time);
        printf("  \033[1;31mCooldown active!\033[0m Please wait %d seconds to mine again.\n", remain);
        return 1;
    }

    printf("  \033[1;33m[Mining Quest Started]\033[0m swing pickaxe...\n");
    fflush(stdout);

    /* Animation loop */
    for (int i = 1; i <= 10; i++) {
        printf("  Progress: [");
        for (int j = 0; j < 10; j++) {
            if (j < i) {
                printf("\033[1;32m█\033[0m");
            } else {
                printf("░");
            }
        }
        printf("] %d%%\r", i * 10);
        fflush(stdout);
        usleep(250000); /* 250ms */
    }
    printf("\n");

    /* Yield calculations */
    int base_xp = 15 + state.level * 2;
    int base_gold = 10 + rand() % 20;

    /* Mining skill boosts gold by 25% per level */
    if (state.skill_mining > 0) {
        base_gold = (int)(base_gold * (1.0 + state.skill_mining * 0.25));
    }

    state.xp += base_xp;
    state.gold += base_gold;
    state.last_mine_time = now;

    printf("  \033[1;32mQuest Complete!\033[0m Found \033[1;33m%d Gold\033[0m and earned \033[1;34m%d XP\033[0m.\n", base_gold, base_xp);

    /* Level Up Check */
    if (state.xp >= state.xp_needed) {
        state.xp -= state.xp_needed;
        state.level++;
        state.xp_needed = 100 + state.level * 50;
        state.max_hp += 10;
        state.hp = state.max_hp;
        state.max_mana += 5;
        state.mana = state.max_mana;
        state.skill_points++;

        printf("\n");
        printf("  \033[1;33m┌──────────────────────────────────────────────┐\033[0m\n");
        printf("  \033[1;33m│                  LEVEL UP!                   │\033[0m\n");
        printf("  \033[1;33m├──────────────────────────────────────────────┤\033[0m\n");
        printf("  \033[1;33m│\033[0m  You reached \033[1;32mLevel %d\033[0m!                         \033[1;33m│\033[0m\n", state.level);
        printf("  \033[1;33m│\033[0m  Max HP +10, Max Mana +5                    \033[1;33m│\033[0m\n");
        printf("  \033[1;33m│\033[0m  Gained \033[1;35m+1 Skill Point\033[0m                       \033[1;33m│\033[0m\n");
        printf("  \033[1;33m└──────────────────────────────────────────────┘\033[0m\n");
        printf("\n");
    }

    save_rpg_state(&state);
    return 0;
}

int builtin_rpg(const parsed_cmd_t *cmd)
{
    rpg_state_t state;
    load_rpg_state(&state);

    if (cmd->argc < 2) {
        printf("\n  \033[1;35mWelcome to BotOS Terminal RPG Subsystem\033[0m\n");
        printf("  =======================================\n");
        printf("  Usage: rpg <command> [args]\n\n");
        printf("  Commands:\n");
        printf("    \033[1;36mhero\033[0m         - Display character sheet & ASCII equipment\n");
        printf("    \033[1;36mmining\033[0m       - Daily quest: mine for Gold & XP\n");
        printf("    \033[1;36mshop\033[0m         - View the gear store\n");
        printf("    \033[1;36mbuy <id>\033[0m     - Purchase gear from the shop\n");
        printf("    \033[1;36mskills\033[0m       - View and allocate skill points\n");
        printf("    \033[1;36mupgrade <id>\033[0m - Allocate point to skill <id>\n");
        printf("    \033[1;36mpet\033[0m          - View pet status & command options\n");
        printf("    \033[1;36mpet adopt <slime/cat/dragon> <name>\033[0m\n");
        printf("    \033[1;36mpet feed\033[0m     - Feed pet companion (5 Gold)\n");
        printf("    \033[1;36mpet play\033[0m     - Play with pet (10 Mana)\n\n");
        return 0;
    }

    const char *sub = cmd->argv[1];

    if (strcmp(sub, "hero") == 0) {
        return builtin_hero(cmd);
    } 
    else if (strcmp(sub, "mining") == 0) {
        return builtin_mining(cmd);
    } 
    else if (strcmp(sub, "shop") == 0) {
        printf("\n  \033[1;33m[ BotOS RPG Gear Store ]\033[0m\n");
        printf("  ========================\n");
        printf("  Your Gold: %d Gold\n\n", state.gold);
        printf("  \033[1;36m1) Iron Sword\033[0m   - Cost: 50g  %s\n", state.has_sword ? "(\033[1;32mOwned\033[0m)" : "");
        printf("     Equips a glowing cyan blade (+25%% Mining Gold)\n\n");
        printf("  \033[1;36m2) Knight Helmet\033[0m - Cost: 75g  %s\n", state.has_helmet ? "(\033[1;32mOwned\033[0m)" : "");
        printf("     Equips a protective gold helmet (+30 Max HP)\n\n");
        printf("  \033[1;36m3) Steel Shield\033[0m  - Cost: 100g %s\n", state.has_shield ? "(\033[1;32mOwned\033[0m)" : "");
        printf("     Equips an iron shield (+15 Max Mana)\n\n");
        printf("  \033[1;36m4) Golden Armor\033[0m  - Cost: 150g %s\n", state.has_armor ? "(\033[1;32mOwned\033[0m)" : "");
        printf("     Equips full plate defense (+50 Max HP, +20 Max Mana)\n\n");
        printf("  Type 'rpg buy <id>' to purchase an item.\n\n");
        return 0;
    } 
    else if (strcmp(sub, "buy") == 0) {
        if (cmd->argc < 3) {
            printf("  Usage: rpg buy <id>  (e.g., rpg buy 1)\n");
            return 1;
        }
        int id = atoi(cmd->argv[2]);
        if (id == 1) {
            if (state.has_sword) { printf("  You already own a Sword!\n"); return 1; }
            if (state.gold < 50) { printf("  Not enough Gold!\n"); return 1; }
            state.gold -= 50;
            state.has_sword = 1;
            printf("  \033[1;32mPurchased Iron Sword!\033[0m Your ASCII hero is now armed.\n");
        } else if (id == 2) {
            if (state.has_helmet) { printf("  You already own a Helmet!\n"); return 1; }
            if (state.gold < 75) { printf("  Not enough Gold!\n"); return 1; }
            state.gold -= 75;
            state.has_helmet = 1;
            state.max_hp += 30;
            state.hp += 30;
            printf("  \033[1;32mPurchased Knight Helmet!\033[0m Max HP increased by 30.\n");
        } else if (id == 3) {
            if (state.has_shield) { printf("  You already own a Shield!\n"); return 1; }
            if (state.gold < 100) { printf("  Not enough Gold!\n"); return 1; }
            state.gold -= 100;
            state.has_shield = 1;
            state.max_mana += 15;
            state.mana += 15;
            printf("  \033[1;32mPurchased Steel Shield!\033[0m Max Mana increased by 15.\n");
        } else if (id == 4) {
            if (state.has_armor) { printf("  You already own Armor!\n"); return 1; }
            if (state.gold < 150) { printf("  Not enough Gold!\n"); return 1; }
            state.gold -= 150;
            state.has_armor = 1;
            state.max_hp += 50;
            state.hp += 50;
            state.max_mana += 20;
            state.mana += 20;
            printf("  \033[1;32mPurchased Golden Armor!\033[0m HP and Mana caps boosted.\n");
        } else {
            printf("  Invalid item ID!\n");
            return 1;
        }
        save_rpg_state(&state);
        return 0;
    } 
    else if (strcmp(sub, "skills") == 0) {
        printf("\n  \033[1;35m[ Hero Skill Tree ]\033[0m\n");
        printf("  ===================\n");
        printf("  Available Skill Points: \033[1;32m%d\033[0m\n\n", state.skill_points);
        printf("  \033[1;36m1) Mining Power\033[0m   (Level %d/5)\n", state.skill_mining);
        printf("     Increases gold yield from mining quests by +25%% per level.\n\n");
        printf("  \033[1;36m2) Toughness\033[0m      (Level %d/5)\n", state.skill_toughness);
        printf("     Boosts maximum HP capacity by +15 per level.\n\n");
        printf("  \033[1;36m3) Mana Expansion\033[0m (Level %d/5)\n", state.skill_mana);
        printf("     Boosts maximum Mana capacity by +10 per level.\n\n");
        printf("  Type 'rpg upgrade <id>' to allocate a skill point.\n\n");
        return 0;
    } 
    else if (strcmp(sub, "upgrade") == 0) {
        if (cmd->argc < 3) {
            printf("  Usage: rpg upgrade <id>  (e.g., rpg upgrade 1)\n");
            return 1;
        }
        if (state.skill_points < 1) {
            printf("  You have no Skill Points! Level up to earn more.\n");
            return 1;
        }
        int id = atoi(cmd->argv[2]);
        if (id == 1) {
            if (state.skill_mining >= 5) { printf("  Skill already at max level!\n"); return 1; }
            state.skill_mining++;
            state.skill_points--;
            printf("  Upgraded \033[1;32mMining Power\033[0m to level %d!\n", state.skill_mining);
        } else if (id == 2) {
            if (state.skill_toughness >= 5) { printf("  Skill already at max level!\n"); return 1; }
            state.skill_toughness++;
            state.skill_points--;
            state.max_hp += 15;
            state.hp += 15;
            printf("  Upgraded \033[1;32mToughness\033[0m to level %d! Max HP increased.\n", state.skill_toughness);
        } else if (id == 3) {
            if (state.skill_mana >= 5) { printf("  Skill already at max level!\n"); return 1; }
            state.skill_mana++;
            state.skill_points--;
            state.max_mana += 10;
            state.mana += 10;
            printf("  Upgraded \033[1;32mMana Expansion\033[0m to level %d! Max Mana increased.\n", state.skill_mana);
        } else {
            printf("  Invalid skill ID!\n");
            return 1;
        }
        save_rpg_state(&state);
        return 0;
    } 
    else if (strcmp(sub, "pet") == 0) {
        if (cmd->argc >= 4 && strcmp(cmd->argv[2], "adopt") == 0) {
            if (cmd->argc < 5) {
                printf("  Usage: rpg pet adopt <slime/cat/dragon> <name>\n");
                return 1;
            }
            if (state.pet_type != 0) {
                printf("  You already have a pet companion!\n");
                return 1;
            }
            const char *type_str = cmd->argv[3];
            int ptype = 0;
            if (strcmp(type_str, "slime") == 0) ptype = 1;
            else if (strcmp(type_str, "cat") == 0) ptype = 2;
            else if (strcmp(type_str, "dragon") == 0) ptype = 3;
            else {
                printf("  Unknown pet type! (Available: slime, cat, dragon)\n");
                return 1;
            }
            
            state.pet_type = ptype;
            strncpy(state.pet_name, cmd->argv[4], sizeof(state.pet_name) - 1);
            state.pet_name[sizeof(state.pet_name) - 1] = '\0';
            state.pet_level = 1;
            state.pet_love = 10;
            
            printf("  \033[1;32mAdoption complete!\033[0m \033[1;36m%s\033[0m has joined you on your journey.\n", state.pet_name);
            save_rpg_state(&state);
            return 0;
        }
        
        if (cmd->argc >= 3 && strcmp(cmd->argv[2], "feed") == 0) {
            if (state.pet_type == 0) { printf("  You have no pet to feed!\n"); return 1; }
            if (state.gold < 5) { printf("  Feeding costs 5 Gold. Not enough gold!\n"); return 1; }
            state.gold -= 5;
            state.pet_love += 15;
            printf("  You fed \033[1;36m%s\033[0m some delicious cyber-kibble. (+15 Friendship)\n", state.pet_name);
            
            if (state.pet_love >= 100) {
                state.pet_level++;
                state.pet_love = 0;
                printf("  \033[1;33mYour pet leveled up!\033[0m %s is now Level %d.\n", state.pet_name, state.pet_level);
            }
            save_rpg_state(&state);
            return 0;
        }

        if (cmd->argc >= 3 && strcmp(cmd->argv[2], "play") == 0) {
            if (state.pet_type == 0) { printf("  You have no pet to play with!\n"); return 1; }
            if (state.mana < 10) { printf("  Playing costs 10 Mana. Not enough mana!\n"); return 1; }
            state.mana -= 10;
            state.pet_love += 25;
            printf("  You played with \033[1;36m%s\033[0m using interactive lights. (+25 Friendship)\n", state.pet_name);
            
            if (state.pet_love >= 100) {
                state.pet_level++;
                state.pet_love = 0;
                printf("  \033[1;33mYour pet leveled up!\033[0m %s is now Level %d.\n", state.pet_name, state.pet_level);
            }
            save_rpg_state(&state);
            return 0;
        }

        /* Show Pet Status */
        printf("\n  \033[1;36m[ Pet Companion Status ]\033[0m\n");
        printf("  ========================\n");
        if (state.pet_type == 0) {
            printf("  You do not have a pet yet!\n");
            printf("  Adopt one with: \033[1;32mrpg pet adopt <slime/cat/dragon> <name>\033[0m\n\n");
        } else {
            printf("  Name       : \033[1;36m%s\033[0m\n", state.pet_name);
            printf("  Type       : %s\n", state.pet_type == 1 ? "Slime" : (state.pet_type == 2 ? "Cat" : "Dragon"));
            printf("  Level      : %d\n", state.pet_level);
            printf("  Friendship : %d / 100\n\n", state.pet_love);
            printf("  Care options:\n");
            printf("    \033[1;32mrpg pet feed\033[0m - Feed (Costs 5 Gold, +15 Friendship)\n");
            printf("    \033[1;32mrpg pet play\033[0m - Play (Costs 10 Mana, +25 Friendship)\n\n");
        }
        return 0;
    }

    printf("  Unknown subcommand '%s'. Type 'rpg' for help.\n", sub);
    return 1;
}

int builtin_get_all_names(const char **names, int max_names)
{
    int count = 0;
    for (int i = 0; g_builtins[i].name && count < max_names; i++) {
        names[count++] = g_builtins[i].name;
    }
    return count;
}
