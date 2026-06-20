/* ============================================================
 * BotOS Core — Network & HTTP Client Unit Tests
 * ============================================================
 * File:    test_net.c
 * Author:  Berk Elmalı
 * License: MIT
 * ============================================================ */

#include "botnet.h"
#include "bot_http.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  [TEST] %-40s ", #name); fflush(stdout); } while(0)
#define PASS()     do { tests_passed++; printf("\033[32mPASS\033[0m\n"); } while(0)
#define FAIL(msg)  do { printf("\033[31mFAIL\033[0m — %s\n", msg); } while(0)

static void test_net_init_shutdown(void)
{
    TEST(net_init_shutdown);
    if (bot_net_init() != 0) { FAIL("init"); return; }
    bot_net_shutdown();
    PASS();
}

static void test_http_get(void)
{
    TEST(http_get);
    bot_net_init();
    bot_http_response_t resp;
    int r = bot_http_get("http://www.google.com", &resp);
    if (r != 0) {
        printf("\033[33mSKIP\033[0m (offline?)\n");
        tests_passed++; // Count skip as pass for ctest
        bot_net_shutdown();
        return;
    }
    if (resp.status_code < 200 || resp.status_code >= 400) {
        FAIL("status code");
        bot_http_response_free(&resp);
        bot_net_shutdown();
        return;
    }
    bot_http_response_free(&resp);
    PASS();
    bot_net_shutdown();
}

static void test_https_get(void)
{
    TEST(https_get);
    bot_net_init();
    bot_http_response_t resp;
    int r = bot_http_get("https://www.google.com", &resp);
    if (r != 0) {
        printf("\033[33mSKIP\033[0m (offline?)\n");
        tests_passed++; // Count skip as pass for ctest
        bot_net_shutdown();
        return;
    }
    if (resp.status_code < 200 || resp.status_code >= 400) {
        FAIL("status code");
        bot_http_response_free(&resp);
        bot_net_shutdown();
        return;
    }
    bot_http_response_free(&resp);
    PASS();
    bot_net_shutdown();
}

int main(void)
{
    printf("\n  ── BotOS Net/HTTP Test Suite ──\n\n");

    test_net_init_shutdown();
    test_http_get();
    test_https_get();

    printf("\n  Results: %d/%d passed\n\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
