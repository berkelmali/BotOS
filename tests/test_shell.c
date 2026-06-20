/* ============================================================
 * BotOS Core — Shell Parser Tests
 * ============================================================
 * File:    test_shell.c
 * Author:  Berk Elmalı
 * License: MIT
 * ============================================================ */

#include "parser.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  [TEST] %-40s ", #name); fflush(stdout); } while(0)
#define PASS()     do { tests_passed++; printf("\033[32mPASS\033[0m\n"); } while(0)
#define FAIL(msg)  do { printf("\033[31mFAIL\033[0m — %s\n", msg); } while(0)

static void test_parser_empty(void)
{
    TEST(parser_is_empty);
    if (!parser_is_empty(""))    { FAIL("empty string"); return; }
    if (!parser_is_empty("   ")) { FAIL("whitespace"); return; }
    if (parser_is_empty("ls"))   { FAIL("non-empty"); return; }
    PASS();
}

static void test_parser_pybridge_detect(void)
{
    TEST(parser_is_pybridge);
    if (!parser_is_pybridge("!sysinfo"))   { FAIL("!sysinfo"); return; }
    if (!parser_is_pybridge("  !test"))     { FAIL("  !test"); return; }
    if (parser_is_pybridge("ls"))           { FAIL("ls false positive"); return; }
    PASS();
}

static void test_parser_simple_command(void)
{
    TEST(parser_simple_command);
    char input[] = "ls -la /tmp";
    pipeline_t pl;
    int ret = parser_parse_line(input, &pl);
    if (ret != 0) { FAIL("parse failed"); return; }
    if (pl.cmd_count != 1) { FAIL("cmd_count != 1"); return; }
    if (pl.commands[0].argc != 3) { FAIL("argc != 3"); return; }
    if (strcmp(pl.commands[0].argv[0], "ls") != 0) { FAIL("argv[0]"); return; }
    if (strcmp(pl.commands[0].argv[1], "-la") != 0) { FAIL("argv[1]"); return; }
    parser_free_pipeline(&pl);
    PASS();
}

static void test_parser_pipeline(void)
{
    TEST(parser_pipeline);
    char input[] = "cat file.txt | grep hello | wc -l";
    pipeline_t pl;
    int ret = parser_parse_line(input, &pl);
    if (ret != 0) { FAIL("parse failed"); return; }
    if (pl.cmd_count != 3) { FAIL("cmd_count != 3"); return; }
    if (strcmp(pl.commands[0].argv[0], "cat") != 0) { FAIL("cmd[0]"); return; }
    if (strcmp(pl.commands[1].argv[0], "grep") != 0) { FAIL("cmd[1]"); return; }
    if (strcmp(pl.commands[2].argv[0], "wc") != 0) { FAIL("cmd[2]"); return; }
    parser_free_pipeline(&pl);
    PASS();
}

static void test_parser_background(void)
{
    TEST(parser_background);
    char input[] = "sleep 10 &";
    pipeline_t pl;
    parser_parse_line(input, &pl);
    if (!pl.commands[0].background) { FAIL("not bg"); return; }
    parser_free_pipeline(&pl);
    PASS();
}

int main(void)
{
    printf("\n  ── BotOS Shell Parser Test Suite ──\n\n");

    test_parser_empty();
    test_parser_pybridge_detect();
    test_parser_simple_command();
    test_parser_pipeline();
    test_parser_background();

    printf("\n  Results: %d/%d passed\n\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
