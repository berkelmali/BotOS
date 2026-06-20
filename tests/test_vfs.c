/* ============================================================
 * BotOS Core — VFS Unit Tests
 * ============================================================
 * File:    test_vfs.c
 * Author:  Berk Elmalı
 * License: MIT
 * ============================================================ */

#include "bot_vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int tests_run    = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  [TEST] %-40s ", #name); \
        fflush(stdout); \
    } while(0)

#define PASS() \
    do { \
        tests_passed++; \
        printf("\033[32mPASS\033[0m\n"); \
    } while(0)

#define FAIL(msg) \
    do { \
        printf("\033[31mFAIL\033[0m — %s\n", msg); \
    } while(0)

/* ── Tests ───────────────────────────────────────────────── */

static void test_vfs_init_shutdown(void)
{
    TEST(vfs_init_shutdown);
    int ret = bot_vfs_init();
    if (ret != 0) { FAIL("init returned non-zero"); return; }

    bot_vfs_shutdown();

    /* Should be safe to call twice */
    bot_vfs_shutdown();
    PASS();
}

static int dummy_open(const char *path, int flags) { (void)path; (void)flags; return 0; }
static ssize_t dummy_read(int fd, void *buf, size_t count) { (void)fd; (void)buf; (void)count; return 0; }
static int dummy_close(int fd) { (void)fd; return 0; }

static void test_vfs_register_driver(void)
{
    TEST(vfs_register_driver);
    bot_vfs_init();

    vfs_driver_t dummy = {
        .name = "dummy",
        .open = dummy_open,
        .read = dummy_read,
        .close = dummy_close
    };

    int ret = bot_vfs_register_driver(&dummy);
    if (ret != 0) { FAIL("register returned non-zero"); bot_vfs_shutdown(); return; }

    PASS();
    bot_vfs_shutdown();
}

static void test_vfs_mount_unmount(void)
{
    TEST(vfs_mount_unmount);
    bot_vfs_init();

    extern const vfs_driver_t vfs_ramfs_driver;
    bot_vfs_register_driver(&vfs_ramfs_driver);

    int ret = bot_vfs_mount("/ram", "ramfs");
    if (ret != 0) { FAIL("mount failed"); bot_vfs_shutdown(); return; }

    ret = bot_vfs_unmount("/ram");
    if (ret != 0) { FAIL("unmount failed"); bot_vfs_shutdown(); return; }

    PASS();
    bot_vfs_shutdown();
}

static void test_vfs_ramfs_read_write(void)
{
    TEST(vfs_ramfs_read_write);
    bot_vfs_init();

    extern const vfs_driver_t vfs_ramfs_driver;
    bot_vfs_register_driver(&vfs_ramfs_driver);
    bot_vfs_mount("/ram", "ramfs");

    /* Write */
    int fd = bot_open("/ram/test.txt", BOT_O_RDWR | BOT_O_CREAT);
    if (fd < 0) { FAIL("open failed"); bot_vfs_shutdown(); return; }

    const char *msg = "Hello BotOS!";
    ssize_t written = bot_write(fd, msg, strlen(msg));
    if (written != (ssize_t)strlen(msg)) { FAIL("write size mismatch"); bot_close(fd); bot_vfs_shutdown(); return; }

    bot_close(fd);

    /* Read back */
    fd = bot_open("/ram/test.txt", BOT_O_RDONLY);
    if (fd < 0) { FAIL("reopen failed"); bot_vfs_shutdown(); return; }

    char buf[64] = {0};
    ssize_t nread = bot_read(fd, buf, sizeof(buf));
    if (nread != (ssize_t)strlen(msg)) { FAIL("read size mismatch"); bot_close(fd); bot_vfs_shutdown(); return; }
    if (strcmp(buf, msg) != 0) { FAIL("data mismatch"); bot_close(fd); bot_vfs_shutdown(); return; }

    bot_close(fd);
    PASS();
    bot_vfs_shutdown();
}

static void test_vfs_ramfs_stat(void)
{
    TEST(vfs_ramfs_stat);
    bot_vfs_init();

    extern const vfs_driver_t vfs_ramfs_driver;
    bot_vfs_register_driver(&vfs_ramfs_driver);
    bot_vfs_mount("/ram", "ramfs");

    int fd = bot_open("/ram/stat_test.txt", BOT_O_RDWR | BOT_O_CREAT);
    bot_write(fd, "12345", 5);
    bot_close(fd);

    bot_stat_t st;
    int ret = bot_stat("/ram/stat_test.txt", &st);
    if (ret != 0) { FAIL("stat failed"); bot_vfs_shutdown(); return; }
    if (st.size != 5) { FAIL("size mismatch"); bot_vfs_shutdown(); return; }
    if (st.type != BOT_FT_REGULAR) { FAIL("type mismatch"); bot_vfs_shutdown(); return; }

    PASS();
    bot_vfs_shutdown();
}

/* ── Runner ──────────────────────────────────────────────── */

int main(void)
{
    printf("\n  ── BotOS VFS Test Suite ──\n\n");

    test_vfs_init_shutdown();
    test_vfs_register_driver();
    test_vfs_mount_unmount();
    test_vfs_ramfs_read_write();
    test_vfs_ramfs_stat();

    printf("\n  Results: %d/%d passed\n\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
