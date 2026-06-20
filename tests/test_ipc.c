/* ============================================================
 * BotOS Core — IPC Unit Tests
 * ============================================================
 * File:    test_ipc.c
 * Author:  Berk Elmalı
 * License: MIT
 * ============================================================ */

#include "bot_ipc.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

static int tests_run = 0, tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  [TEST] %-40s ", #name); fflush(stdout); } while(0)
#define PASS()     do { tests_passed++; printf("\033[32mPASS\033[0m\n"); } while(0)
#define FAIL(msg)  do { printf("\033[31mFAIL\033[0m — %s\n", msg); } while(0)

static void test_ipc_init_shutdown(void)
{
    TEST(ipc_init_shutdown);
    if (bot_ipc_init() != 0) { FAIL("init"); return; }
    bot_ipc_shutdown();
    bot_ipc_shutdown();  /* Double shutdown should be safe */
    PASS();
}

static void test_mq_create_close(void)
{
    TEST(mq_create_close);
    bot_ipc_init();
    int mq = bot_mq_create("/test_queue", 10, 256);
    if (mq < 0) { FAIL("create"); bot_ipc_shutdown(); return; }
    if (bot_mq_close(mq) != 0) { FAIL("close"); bot_ipc_shutdown(); return; }
    PASS();
    bot_ipc_shutdown();
}

static void test_mq_send_recv(void)
{
    TEST(mq_send_recv);
    bot_ipc_init();
    int mq = bot_mq_create("/data_queue", 8, 64);

    const char *msg = "hello ipc";
    if (bot_mq_send(mq, msg, strlen(msg) + 1, BOT_IPC_PRIO_NORMAL) != 0) {
        FAIL("send"); bot_ipc_shutdown(); return;
    }

    char buf[64] = {0};
    int prio;
    ssize_t n = bot_mq_recv(mq, buf, sizeof(buf), &prio);
    if (n < 0) { FAIL("recv"); bot_ipc_shutdown(); return; }
    if (strcmp(buf, msg) != 0) { FAIL("data mismatch"); bot_ipc_shutdown(); return; }

    bot_mq_close(mq);
    PASS();
    bot_ipc_shutdown();
}

static void test_mq_open_existing(void)
{
    TEST(mq_open_existing);
    bot_ipc_init();
    bot_mq_create("/named_q", 4, 32);

    int mq2 = bot_mq_open("/named_q");
    if (mq2 < 0) { FAIL("open existing"); bot_ipc_shutdown(); return; }

    PASS();
    bot_ipc_shutdown();
}

static void test_shm_create_attach(void)
{
    TEST(shm_create_attach);
    bot_ipc_init();
    int seg = bot_shm_create("/test_shm", 4096);
    if (seg < 0) { FAIL("create"); bot_ipc_shutdown(); return; }

    void *addr = bot_shm_attach("/test_shm");
    if (!addr) { FAIL("attach"); bot_ipc_shutdown(); return; }

    /* Write and verify */
    memcpy(addr, "shared_data", 12);
    void *addr2 = bot_shm_attach("/test_shm");
    if (memcmp(addr2, "shared_data", 12) != 0) { FAIL("data"); bot_ipc_shutdown(); return; }

    bot_shm_detach("/test_shm");
    bot_shm_destroy("/test_shm");
    PASS();
    bot_ipc_shutdown();
}

static void test_shm_info(void)
{
    TEST(shm_info);
    bot_ipc_init();
    bot_shm_create("/info_shm", 2048);

    bot_shm_info_t info;
    if (bot_shm_info("/info_shm", &info) != 0) { FAIL("info"); bot_ipc_shutdown(); return; }
    if (info.size != 2048) { FAIL("size mismatch"); bot_ipc_shutdown(); return; }

    bot_shm_destroy("/info_shm");
    PASS();
    bot_ipc_shutdown();
}

static void test_shm_cross_process(void)
{
    TEST(shm_cross_process);
    bot_ipc_init();
    
    int seg = bot_shm_create("/cross_shm", 4096);
    if (seg < 0) { FAIL("create"); bot_ipc_shutdown(); return; }

    void *addr = bot_shm_attach("/cross_shm");
    if (!addr) { FAIL("attach parent"); bot_ipc_shutdown(); return; }

    memset(addr, 0, 4096);
    strcpy(addr, "parent_hello");

    pid_t pid = fork();
    if (pid < 0) {
        FAIL("fork failed");
        bot_shm_detach("/cross_shm");
        bot_shm_destroy("/cross_shm");
        bot_ipc_shutdown();
        return;
    }

    if (pid == 0) {
        /* Child process */
        void *child_addr = bot_shm_attach("/cross_shm");
        if (!child_addr) {
            _exit(1);
        }
        
        if (strcmp((char *)child_addr, "parent_hello") != 0) {
            _exit(2);
        }
        
        strcpy((char *)child_addr, "child_hello");
        bot_shm_detach("/cross_shm");
        _exit(0);
    } else {
        /* Parent process */
        int status;
        waitpid(pid, &status, 0);
        
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            FAIL("child exit error");
            bot_shm_detach("/cross_shm");
            bot_shm_destroy("/cross_shm");
            bot_ipc_shutdown();
            return;
        }
        
        if (strcmp((char *)addr, "child_hello") != 0) {
            FAIL("data mismatch from child");
            bot_shm_detach("/cross_shm");
            bot_shm_destroy("/cross_shm");
            bot_ipc_shutdown();
            return;
        }
        
        bot_shm_detach("/cross_shm");
        bot_shm_destroy("/cross_shm");
        PASS();
        bot_ipc_shutdown();
    }
}

int main(void)
{
    printf("\n  ── BotOS IPC Test Suite ──\n\n");

    test_ipc_init_shutdown();
    test_mq_create_close();
    test_mq_send_recv();
    test_mq_open_existing();
    test_shm_create_attach();
    test_shm_info();
    test_shm_cross_process();

    printf("\n  Results: %d/%d passed\n\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
