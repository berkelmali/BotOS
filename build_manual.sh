#!/bin/bash
# Manual build script replicating the (now-fixed) CMakeLists.txt build graph
set -uo pipefail

ROOT="/home/claude/OS"
OUT="/home/claude/build"
mkdir -p "$OUT/lib" "$OUT/bin" "$OUT/tests" "$OUT/obj"

CC=gcc
STD_FLAGS="-std=c11 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE -DBOTOS_VERSION=\"0.3.0\""
WARN_FLAGS="-Wall -Wextra -Werror -Wpedantic -Wshadow -Wdouble-promotion -Wformat=2 -Wno-unused-parameter -fstack-protector-strong -fPIC"
CFLAGS="$STD_FLAGS $WARN_FLAGS"

FAIL=0
PASS=0
FAILED_ITEMS=()

compile_obj() {
    local src="$1" obj="$2"; shift 2
    local incs=""
    for i in "$@"; do incs="$incs -I$i"; done
    if eval $CC $CFLAGS $incs -c "$src" -o "$obj" 2> "$obj.log"; then
        PASS=$((PASS+1)); return 0
    else
        FAIL=$((FAIL+1)); FAILED_ITEMS+=("$obj")
        echo "  [FAIL] $src"; sed 's/^/      /' "$obj.log" | head -25
        return 1
    fi
}
link_bin() {
    local out="$1"; shift
    if eval $CC $CFLAGS "$@" -o "$out" 2> "$out.log"; then
        PASS=$((PASS+1)); echo "  [OK] link $(basename "$out")"
    else
        FAIL=$((FAIL+1)); FAILED_ITEMS+=("link:$(basename "$out")")
        echo "  [FAIL] link $(basename "$out")"; sed 's/^/      /' "$out.log" | head -25
    fi
}

echo "=============================================="
echo " Manual Build — BotOS (post-fix, no cmake in sandbox)"
echo "=============================================="

echo ""; echo "--- bot_ipc ---"
compile_obj "$ROOT/core/ipc/src/ipc.c" "$OUT/obj/ipc.o" "$ROOT/core/ipc/include"
ar rcs "$OUT/lib/libbot_ipc.a" "$OUT/obj/ipc.o" 2>/dev/null

echo ""; echo "--- bot_vfs ---"
compile_obj "$ROOT/core/vfs/src/vfs_core.c"  "$OUT/obj/vfs_core.o"  "$ROOT/core/vfs/include" "$ROOT/core/ipc/include"
compile_obj "$ROOT/core/vfs/src/vfs_ext4.c"  "$OUT/obj/vfs_ext4.o"  "$ROOT/core/vfs/include" "$ROOT/core/ipc/include"
compile_obj "$ROOT/core/vfs/src/vfs_ramfs.c" "$OUT/obj/vfs_ramfs.o" "$ROOT/core/vfs/include" "$ROOT/core/ipc/include"
compile_obj "$ROOT/core/vfs/src/vfs_netfs.c" "$OUT/obj/vfs_netfs.o" "$ROOT/core/vfs/include" "$ROOT/core/ipc/include"
ar rcs "$OUT/lib/libbot_vfs.a" "$OUT/obj/vfs_core.o" "$OUT/obj/vfs_ext4.o" "$OUT/obj/vfs_ramfs.o" "$OUT/obj/vfs_netfs.o" 2>/dev/null

echo ""; echo "--- bot_log ---"
compile_obj "$ROOT/services/botlog/src/botlog.c" "$OUT/obj/botlog.o" "$ROOT/services/botlog/include"
ar rcs "$OUT/lib/libbot_log.a" "$OUT/obj/botlog.o" 2>/dev/null

echo ""; echo "--- bot_init (exe) ---"
compile_obj "$ROOT/core/init/src/init.c" "$OUT/obj/init.o" "$ROOT/core/init/include" "$ROOT/core/vfs/include" "$ROOT/core/ipc/include"
link_bin "$OUT/bin/bot_init" "$OUT/obj/init.o" -L"$OUT/lib" -lbot_vfs -lbot_ipc -lpthread -lrt

echo ""; echo "--- libbot ---"
compile_obj "$ROOT/sdk/libbot/src/bot_io.c"     "$OUT/obj/bot_io.o"     "$ROOT/sdk/libbot/include" "$ROOT/core/vfs/include" "$ROOT/services/botlog/include" "$ROOT/core/ipc/include"
compile_obj "$ROOT/sdk/libbot/src/bot_string.c" "$OUT/obj/bot_string.o" "$ROOT/sdk/libbot/include"
compile_obj "$ROOT/sdk/libbot/src/bot_mem.c"    "$OUT/obj/bot_mem.o"    "$ROOT/sdk/libbot/include"
ar rcs "$OUT/lib/libbot.a" "$OUT/obj/bot_io.o" "$OUT/obj/bot_string.o" "$OUT/obj/bot_mem.o" 2>/dev/null

echo ""; echo "--- bot_ui (X11 backend now wired up — mirrors the CMake fix) ---"
BOTUI_INC="$ROOT/sdk/botui/include $ROOT/sdk/libbot/include"
FT_CFLAGS=$(pkg-config --cflags freetype2 2>/dev/null)
FT_LIBS=$(pkg-config --libs freetype2 2>/dev/null)
X11_CFLAGS="$CFLAGS -DBOTOS_HAS_X11=1"
for f in window canvas event; do
    if eval $CC $X11_CFLAGS $(printf -- '-I%s ' $BOTUI_INC) -c "$ROOT/sdk/botui/src/$f.c" -o "$OUT/obj/$f.o" 2> "$OUT/obj/$f.o.log"; then
        PASS=$((PASS+1)); echo "  [OK] $f.c (X11)"
    else
        FAIL=$((FAIL+1)); FAILED_ITEMS+=("$OUT/obj/$f.o"); echo "  [FAIL] $f.c"; sed 's/^/      /' "$OUT/obj/$f.o.log" | head -25
    fi
done
# widget.c compiled without eval: the FreeType font-path define contains
# double quotes the C preprocessor needs literally, which eval's second
# round of shell parsing was stripping (the classic double-eval quoting
# trap) — direct array-based invocation sidesteps that entirely.
if $CC $CFLAGS -DBOTOS_HAS_X11=1 -DBOTOS_HAS_FREETYPE=1 \
        -DBOTOS_FONT_DEV_PATH="\"$ROOT/sdk/botui/assets/fonts/DejaVuSansMono.ttf\"" \
        $FT_CFLAGS -I"$ROOT/sdk/botui/include" -I"$ROOT/sdk/libbot/include" \
        -c "$ROOT/sdk/botui/src/widget.c" -o "$OUT/obj/widget.o" 2> "$OUT/obj/widget.o.log"; then
    PASS=$((PASS+1)); echo "  [OK] widget.c (X11+FreeType)"
else
    FAIL=$((FAIL+1)); FAILED_ITEMS+=("$OUT/obj/widget.o"); echo "  [FAIL] widget.c"; sed 's/^/      /' "$OUT/obj/widget.o.log" | head -25
fi
ar rcs "$OUT/lib/libbot_ui.a" "$OUT/obj/window.o" "$OUT/obj/canvas.o" "$OUT/obj/event.o" "$OUT/obj/widget.o" 2>/dev/null

echo ""; echo "--- bot_parser ---"
compile_obj "$ROOT/shell/src/parser.c" "$OUT/obj/parser.o" "$ROOT/shell/include"
ar rcs "$OUT/lib/libbot_parser.a" "$OUT/obj/parser.o" 2>/dev/null

echo ""; echo "--- botshell (exe) ---"
SHELL_INC="$ROOT/shell/include $ROOT/core/vfs/include $ROOT/core/ipc/include"
PYINC=$(python3-config --includes 2>/dev/null)
compile_obj "$ROOT/shell/src/botshell.c"  "$OUT/obj/botshell.o"  $SHELL_INC
compile_obj "$ROOT/shell/src/executor.c"  "$OUT/obj/executor.o"  $SHELL_INC
compile_obj "$ROOT/shell/src/builtins.c"  "$OUT/obj/builtins.o"  $SHELL_INC
if eval $CC $CFLAGS -DBOTOS_PYBRIDGE_ENABLED=1 $PYINC -I$ROOT/shell/include -c "$ROOT/shell/src/pybridge.c" -o "$OUT/obj/pybridge.o" 2> "$OUT/obj/pybridge.o.log"; then
    PASS=$((PASS+1)); echo "  [OK] pybridge.c"
else
    FAIL=$((FAIL+1)); FAILED_ITEMS+=("$OUT/obj/pybridge.o"); echo "  [FAIL] pybridge.c"; cat "$OUT/obj/pybridge.o.log"
fi
PYLIBS=$(python3-config --ldflags --embed 2>/dev/null || python3-config --ldflags 2>/dev/null)
link_bin "$OUT/bin/botshell" "$OUT/obj/botshell.o" "$OUT/obj/executor.o" "$OUT/obj/builtins.o" "$OUT/obj/pybridge.o" -L"$OUT/lib" -lbot_parser -lbot_vfs -lbot_ipc -lpthread -lrt $PYLIBS

echo ""; echo "--- bot_notify ---"
compile_obj "$ROOT/services/botnotify/src/bot_notify.c" "$OUT/obj/bot_notify.o" "$ROOT/services/botnotify/include"
ar rcs "$OUT/lib/libbot_notify.a" "$OUT/obj/bot_notify.o" 2>/dev/null
compile_obj "$ROOT/services/botnotify/src/botnotify_cli.c" "$OUT/obj/botnotify_cli.o" "$ROOT/services/botnotify/include"
link_bin "$OUT/bin/botnotify" "$OUT/obj/botnotify_cli.o" -L"$OUT/lib" -lbot_notify

echo ""; echo "--- apps (now linking against the real X11-enabled bot_ui) ---"
APP_INC="$ROOT/sdk/botui/include $ROOT/sdk/libbot/include $ROOT/services/botlog/include $ROOT/services/botnotify/include"
APP_CFLAGS="$CFLAGS -DBOTOS_HAS_X11=1"
for app in botdesk botfiles botinfo botpaint botpkg_gui botsettings botterm editor; do
    EXTRA_LIBS=""
    [ "$app" = "botpaint" ] && EXTRA_LIBS="-lm"
    [ "$app" = "botdesk" ] && EXTRA_LIBS="-lbot_notify"
    if eval $CC $APP_CFLAGS $(printf -- '-I%s ' $APP_INC) -c "$ROOT/apps/$app/src/$app.c" -o "$OUT/obj/$app.o" 2> "$OUT/obj/$app.o.log"; then
        PASS=$((PASS+1))
    else
        FAIL=$((FAIL+1)); FAILED_ITEMS+=("$OUT/obj/$app.o"); echo "  [FAIL] $app.c"; sed 's/^/      /' "$OUT/obj/$app.o.log" | head -25
    fi
    link_bin "$OUT/bin/$app" "$OUT/obj/$app.o" -L"$OUT/lib" -lbot_ui -lbot -lbot_log $EXTRA_LIBS -lX11 $FT_LIBS
done

echo ""; echo "--- tests (excluding test_net, which needs OpenSSL headers unavailable in this sandbox) ---"
TEST_INC="$ROOT/core/vfs/include $ROOT/core/ipc/include $ROOT/shell/include"
compile_obj "$ROOT/tests/test_vfs.c" "$OUT/obj/test_vfs.o" $TEST_INC
link_bin "$OUT/tests/test_vfs" "$OUT/obj/test_vfs.o" -L"$OUT/lib" -lbot_vfs -lbot_ipc -lpthread -lrt

compile_obj "$ROOT/tests/test_shell.c" "$OUT/obj/test_shell.o" $TEST_INC
link_bin "$OUT/tests/test_shell" "$OUT/obj/test_shell.o" -L"$OUT/lib" -lbot_parser

compile_obj "$ROOT/tests/test_ipc.c" "$OUT/obj/test_ipc.o" $TEST_INC
link_bin "$OUT/tests/test_ipc" "$OUT/obj/test_ipc.o" -L"$OUT/lib" -lbot_ipc -lpthread -lrt

compile_obj "$ROOT/services/botpkg/src/manifest.c" "$OUT/obj/pkg_manifest.o" "$ROOT/services/botpkg/include" "$ROOT/services/botlog/include"
compile_obj "$ROOT/services/botpkg/src/resolver.c" "$OUT/obj/pkg_resolver.o" "$ROOT/services/botpkg/include" "$ROOT/services/botlog/include" "$ROOT/services/botnet/include"
compile_obj "$ROOT/services/botpkg/src/sha256.c" "$OUT/obj/pkg_sha256.o" "$ROOT/services/botpkg/include"
ar rcs "$OUT/lib/libbot_pkg_core.a" "$OUT/obj/pkg_manifest.o" "$OUT/obj/pkg_resolver.o" "$OUT/obj/pkg_sha256.o" 2>/dev/null
compile_obj "$ROOT/tests/test_botpkg.c" "$OUT/obj/test_botpkg.o" "$ROOT/services/botpkg/include"
link_bin "$OUT/tests/test_botpkg" "$OUT/obj/test_botpkg.o" -L"$OUT/lib" -lbot_pkg_core

echo ""
echo "=============================================="
echo " FINAL Build summary: $PASS ok, $FAIL failed"
echo "=============================================="
if [ ${#FAILED_ITEMS[@]} -gt 0 ]; then
    printf '  FAILED: %s\n' "${FAILED_ITEMS[@]}"
fi
