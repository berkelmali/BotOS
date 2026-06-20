/* ============================================================
 * BotOS Core — PyBridge Python Interface (Production)
 * ============================================================
 * File:    pybridge.c
 * Layer:   L4 — Development Layer
 * Author:  Berk Elmalı
 * License: MIT
 *
 * Phase 3 — Production C → Python bridge.
 *
 * Two execution modes:
 *
 *   1. EMBEDDED (BOTOS_PYBRIDGE_ENABLED):
 *      Uses CPython API directly via Py_Initialize(),
 *      PyImport_ImportModule(), PyObject_CallFunction().
 *      Zero-latency command dispatch.
 *
 *   2. SUBPROCESS FALLBACK (no Python headers):
 *      Forks a `python3 -c "<command>"` subprocess.
 *      Fully functional but with fork/exec overhead.
 *
 * Magic Commands:
 *   - C-registered handlers with duplicate detection
 *   - Priority over Python dispatch (checked first)
 *   - Built-in !help command listing all registered magics
 *
 * This module is the crown jewel of BotOS — it makes a C
 * shell feel like a Python REPL when you need it to.
 * ============================================================ */

#include "pybridge.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* ── Magic Command Registry ──────────────────────────────── */

#define PB_MAX_MAGIC_CMDS  32
#define PB_NAME_MAX        64
#define PB_HELP_MAX        128

typedef struct {
    int                 active;
    char                name[PB_NAME_MAX];
    magic_cmd_handler_t handler;
    char                help[PB_HELP_MAX];
} magic_cmd_entry_t;

static magic_cmd_entry_t g_magic_cmds[PB_MAX_MAGIC_CMDS];
static int               g_magic_count = 0;

/* ── Internal: Magic Command Helpers ─────────────────────── */

/**
 * Extract command name and arguments from input.
 */
static void pb_split_command(const char *command,
                             char *name_out, size_t name_size,
                             const char **args_out)
{
    const char *space = strchr(command, ' ');
    if (space) {
        size_t len = (size_t)(space - command);
        if (len >= name_size) len = name_size - 1;
        strncpy(name_out, command, len);
        name_out[len] = '\0';
        *args_out = space + 1;
        while (**args_out == ' ') (*args_out)++;
    } else {
        strncpy(name_out, command, name_size - 1);
        name_out[name_size - 1] = '\0';
        *args_out = "";
    }
}

/**
 * Search magic commands for a match.
 * @return  Pointer to the entry, or NULL if not found.
 */
static magic_cmd_entry_t *pb_find_magic(const char *name)
{
    for (int i = 0; i < PB_MAX_MAGIC_CMDS; i++) {
        if (g_magic_cmds[i].active &&
            strcmp(g_magic_cmds[i].name, name) == 0) {
            return &g_magic_cmds[i];
        }
    }
    return NULL;
}

/* ── Built-in Magic: !help ───────────────────────────────── */

static int magic_help_handler(const char *args)
{
    (void)args;

    printf("\n  \033[1m\033[36mPyBridge Magic Commands\033[0m\n\n");

    int found = 0;
    for (int i = 0; i < PB_MAX_MAGIC_CMDS; i++) {
        if (g_magic_cmds[i].active) {
            printf("  \033[33m!%-16s\033[0m %s\n",
                   g_magic_cmds[i].name, g_magic_cmds[i].help);
            found++;
        }
    }

    if (found == 0) {
        printf("  (no magic commands registered)\n");
    }

    printf("\n  \033[2mAny other !command is sent to the Python runtime\033[0m\n\n");
    return 0;
}

/* ── Built-in Magic: !sysinfo ────────────────────────────── */

static int magic_sysinfo_handler(const char *args)
{
    (void)args;

    printf("\n  \033[1m\033[36mBotOS System Information\033[0m\n\n");
    printf("  %-20s %s\n", "OS:", "BotOS Core");
    printf("  %-20s %s\n", "Shell:", "BotShell v0.3.0");
    printf("  %-20s %s\n", "PyBridge:", "Active");

#ifdef BOTOS_PYBRIDGE_ENABLED
    printf("  %-20s %s\n", "Python Mode:", "Embedded (CPython API)");
#else
    printf("  %-20s %s\n", "Python Mode:", "Subprocess (python3 -c)");
#endif

    const char *home = getenv("HOME");
    const char *user = getenv("USER");
    printf("  %-20s %s\n", "User:", user ? user : "(unknown)");
    printf("  %-20s %s\n", "Home:", home ? home : "(unknown)");
    printf("\n");

    return 0;
}

/* ════════════════════════════════════════════════════════════
 *  MODE 1: EMBEDDED PYTHON (CPython API)
 * ════════════════════════════════════════════════════════════ */

#ifdef BOTOS_PYBRIDGE_ENABLED

#include <Python.h>

static int       g_pb_initialized = 0;
static PyObject *g_bridge_module  = NULL;
static PyObject *g_execute_func   = NULL;

int pybridge_init(const char *scripts_dir)
{
    if (g_pb_initialized) return 0;

    /* Initialize magic command registry */
    memset(g_magic_cmds, 0, sizeof(g_magic_cmds));
    g_magic_count = 0;

    /* Add scripts directory to Python path before init */
    if (scripts_dir) {
        /* Set PYTHONPATH so modules are discoverable */
        char env_buf[1024];
        const char *existing = getenv("PYTHONPATH");
        if (existing) {
            snprintf(env_buf, sizeof(env_buf), "%s:%s", scripts_dir, existing);
        } else {
            snprintf(env_buf, sizeof(env_buf), "%s", scripts_dir);
        }
        setenv("PYTHONPATH", env_buf, 1);
    }

    Py_Initialize();

    if (!Py_IsInitialized()) {
        fprintf(stderr, "[pybridge] Failed to initialize Python\n");
        return -1;
    }

    /* Add scripts_dir to sys.path explicitly */
    if (scripts_dir) {
        char path_cmd[512];
        snprintf(path_cmd, sizeof(path_cmd),
                 "import sys; sys.path.insert(0, '%s')", scripts_dir);
        PyRun_SimpleString(path_cmd);
    }

    /* Import the bridge module */
    g_bridge_module = PyImport_ImportModule("runtime");
    if (!g_bridge_module) {
        fprintf(stderr, "[pybridge] Warning: runtime package not found\n");
        PyErr_Print();
        /* Continue — direct Python exec still works */
    }

    /* Get the execute function */
    if (g_bridge_module) {
        g_execute_func = PyObject_GetAttrString(g_bridge_module, "execute");
        if (!g_execute_func || !PyCallable_Check(g_execute_func)) {
            fprintf(stderr, "[pybridge] Warning: runtime.execute() not found\n");
            Py_XDECREF(g_execute_func);
            g_execute_func = NULL;
        }
    }

    /* Register built-in magic commands */
    pybridge_register_magic("help", magic_help_handler,
                            "Show available magic commands");
    pybridge_register_magic("sysinfo", magic_sysinfo_handler,
                            "Display system information");

    g_pb_initialized = 1;
    return 0;
}

void pybridge_shutdown(void)
{
    if (!g_pb_initialized) return;

    Py_XDECREF(g_execute_func);
    Py_XDECREF(g_bridge_module);
    g_execute_func  = NULL;
    g_bridge_module = NULL;

    if (Py_IsInitialized()) {
        Py_Finalize();
    }

    g_pb_initialized = 0;
}

int pybridge_is_available(void)
{
    return g_pb_initialized;
}

int pybridge_execute(const char *command)
{
    if (!g_pb_initialized || !command) return -1;

    /* Skip leading whitespace */
    while (*command == ' ') command++;
    if (*command == '\0') return 0;

    /* 1. Check C-registered magic commands first */
    char cmd_name[PB_NAME_MAX];
    const char *args = "";
    pb_split_command(command, cmd_name, sizeof(cmd_name), &args);

    magic_cmd_entry_t *magic = pb_find_magic(cmd_name);
    if (magic) {
        return magic->handler(args);
    }

    /* 2. Try the Python bridge.execute() function */
    if (g_execute_func) {
        PyObject *py_result = PyObject_CallFunction(g_execute_func, "s", command);
        if (py_result) {
            int ret = 0;
            if (PyLong_Check(py_result)) {
                ret = (int)PyLong_AsLong(py_result);
            }
            Py_DECREF(py_result);
            return ret;
        }
        PyErr_Print();
        return -1;
    }

    /* 3. Fallback: direct PyRun_SimpleString */
    int ret = PyRun_SimpleString(command);
    return (ret == 0) ? 0 : -1;
}

int pybridge_exec_script(const char *path)
{
    if (!g_pb_initialized || !path) return -1;

    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "[pybridge] Cannot open script: %s: %s\n",
                path, strerror(errno));
        return -1;
    }

    int ret = PyRun_SimpleFile(fp, path);
    fclose(fp);
    return (ret == 0) ? 0 : -1;
}

/* ════════════════════════════════════════════════════════════
 *  MODE 2: SUBPROCESS FALLBACK (no CPython headers)
 * ════════════════════════════════════════════════════════════ */

#else /* BOTOS_PYBRIDGE_ENABLED not defined */

#include <unistd.h>
#include <sys/wait.h>

/** Path to the Python interpreter, auto-detected on init. */
static char g_python_path[256] = "";
static int  g_pb_initialized = 0;

/**
 * Detect the Python 3 interpreter path.
 */
static int pb_detect_python(void)
{
    /* Try common locations */
    const char *candidates[] = {
        "/usr/bin/python3",
        "/usr/local/bin/python3",
        "/usr/bin/python",
        NULL
    };

    for (int i = 0; candidates[i]; i++) {
        if (access(candidates[i], X_OK) == 0) {
            strncpy(g_python_path, candidates[i], sizeof(g_python_path) - 1);
            return 0;
        }
    }

    /* Try PATH via which */
    FILE *fp = popen("which python3 2>/dev/null", "r");
    if (fp) {
        if (fgets(g_python_path, sizeof(g_python_path), fp)) {
            /* Strip newline */
            size_t len = strlen(g_python_path);
            if (len > 0 && g_python_path[len - 1] == '\n') {
                g_python_path[len - 1] = '\0';
            }
            pclose(fp);
            if (strlen(g_python_path) > 0) return 0;
        }
        pclose(fp);
    }

    return -1;
}

int pybridge_init(const char *scripts_dir)
{
    (void)scripts_dir;

    if (g_pb_initialized) return 0;

    memset(g_magic_cmds, 0, sizeof(g_magic_cmds));
    g_magic_count = 0;

    if (pb_detect_python() != 0) {
        fprintf(stderr, "[pybridge] Warning: Python 3 not found in PATH\n");
        fprintf(stderr, "[pybridge] Magic commands still available\n");
    } else {
        fprintf(stderr, "[pybridge] Using subprocess mode: %s\n",
                g_python_path);
    }

    /* Register built-in magic commands */
    pybridge_register_magic("help", magic_help_handler,
                            "Show available magic commands");
    pybridge_register_magic("sysinfo", magic_sysinfo_handler,
                            "Display system information");

    g_pb_initialized = 1;
    return 0;
}

void pybridge_shutdown(void)
{
    g_pb_initialized = 0;
    g_python_path[0] = '\0';
}

int pybridge_is_available(void)
{
    return g_pb_initialized;
}

int pybridge_execute(const char *command)
{
    if (!g_pb_initialized || !command) return -1;

    while (*command == ' ') command++;
    if (*command == '\0') return 0;

    /* 1. Check magic commands first */
    char cmd_name[PB_NAME_MAX];
    const char *args = "";
    pb_split_command(command, cmd_name, sizeof(cmd_name), &args);

    magic_cmd_entry_t *magic = pb_find_magic(cmd_name);
    if (magic) {
        return magic->handler(args);
    }

    /* 2. Subprocess: python3 -c "command" */
    if (g_python_path[0] == '\0') {
        fprintf(stderr, "[pybridge] Python not available\n");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("[pybridge] fork");
        return -1;
    }

    if (pid == 0) {
        /* Child: exec python3 -c "..." using environment variable */
        signal(SIGINT,  SIG_DFL);
        signal(SIGQUIT, SIG_DFL);

        setenv("PYBRIDGE_CMD", command, 1);

        const char *wrapper =
            "import os\n"
            "cmd = os.environ.get('PYBRIDGE_CMD', '')\n"
            "try:\n"
            "    co = compile(cmd, '<string>', 'eval')\n"
            "except SyntaxError:\n"
            "    exec(cmd)\n"
            "else:\n"
            "    res = eval(co)\n"
            "    if res is not None:\n"
            "        print(repr(res))\n";

        execl(g_python_path, "python3", "-c", wrapper, NULL);

        fprintf(stderr, "[pybridge] exec failed: %s\n", strerror(errno));
        _exit(127);
    }

    /* Parent: wait for Python to finish */
    int status;
    pid_t w;
    do {
        w = waitpid(pid, &status, 0);
    } while (w < 0 && errno == EINTR);

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status) == 0 ? 0 : -1;
    }

    return -1;
}

int pybridge_exec_script(const char *path)
{
    if (!g_pb_initialized || !path) return -1;

    if (g_python_path[0] == '\0') {
        fprintf(stderr, "[pybridge] Python not available\n");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("[pybridge] fork");
        return -1;
    }

    if (pid == 0) {
        signal(SIGINT,  SIG_DFL);
        signal(SIGQUIT, SIG_DFL);

        execl(g_python_path, "python3", path, NULL);
        fprintf(stderr, "[pybridge] exec failed: %s\n", strerror(errno));
        _exit(127);
    }

    int status;
    pid_t w;
    do {
        w = waitpid(pid, &status, 0);
    } while (w < 0 && errno == EINTR);

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status) == 0 ? 0 : -1;
    }

    return -1;
}

#endif /* BOTOS_PYBRIDGE_ENABLED */

/* ════════════════════════════════════════════════════════════
 *  MAGIC COMMAND REGISTRATION (shared by both modes)
 * ════════════════════════════════════════════════════════════ */

int pybridge_register_magic(const char *name,
                            magic_cmd_handler_t handler,
                            const char *help)
{
    if (!name || !handler) {
        errno = EINVAL;
        return -1;
    }

    /* Duplicate detection */
    if (pb_find_magic(name) != NULL) {
        errno = EEXIST;
        return -1;
    }

    if (g_magic_count >= PB_MAX_MAGIC_CMDS) {
        errno = ENOMEM;
        return -1;
    }

    /* Find free slot */
    int idx = -1;
    for (int i = 0; i < PB_MAX_MAGIC_CMDS; i++) {
        if (!g_magic_cmds[i].active) { idx = i; break; }
    }
    if (idx < 0) { errno = ENOMEM; return -1; }

    g_magic_cmds[idx].active  = 1;
    g_magic_cmds[idx].handler = handler;

    strncpy(g_magic_cmds[idx].name, name, PB_NAME_MAX - 1);
    g_magic_cmds[idx].name[PB_NAME_MAX - 1] = '\0';

    if (help) {
        strncpy(g_magic_cmds[idx].help, help, PB_HELP_MAX - 1);
        g_magic_cmds[idx].help[PB_HELP_MAX - 1] = '\0';
    }

    g_magic_count++;
    return 0;
}
