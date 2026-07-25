# ============================================================
# BotOS Core — BotUI (GUI Abstraction Library)
# ============================================================

add_library(bot_ui STATIC
    src/window.c
    src/canvas.c
    src/event.c
    src/widget.c
)

target_include_directories(bot_ui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(bot_ui
    PUBLIC
        bot
)

# Optional SDL2 dependency (for Phase 6)
find_package(SDL2 QUIET)
if(SDL2_FOUND)
    target_link_libraries(bot_ui PRIVATE SDL2::SDL2)
    # PUBLIC: consumers (e.g. BotDesk, if it ever grows SDL2-specific
    # code the way it now has X11-specific code) need this visible at
    # their own compile time too, not just inside bot_ui's sources.
    target_compile_definitions(bot_ui PUBLIC BOTOS_HAS_SDL2=1)
    message(STATUS "BotUI: SDL2 found — hardware-accelerated rendering enabled")
else()
    message(STATUS "BotUI: SDL2 not found — using framebuffer stubs")
endif()

# Optional X11 dependency — window.c/event.c implement a full Xlib windowing
# backend guarded by BOTOS_HAS_X11, but nothing previously defined that
# macro, so every desktop build silently fell back to the headless
# /dev/fb0 framebuffer backend and no app ever opened a real window.
# Mirror the SDL2 detection above so the X11 backend actually activates
# on any host that has the X11 development libraries (typical desktop
# Linux), while still falling back to the framebuffer backend for
# headless/embedded targets (e.g. the Buildroot target rootfs) where
# X11 isn't present.
find_package(X11 QUIET)
if(X11_FOUND)
    # PUBLIC, not PRIVATE: BotDesk now acts as a minimal X11 window
    # manager for other apps' windows (reparenting, decorations, the
    # taskbar's window list) and has its own `#ifdef BOTOS_HAS_X11`
    # code and direct `#include <X11/Xlib.h>` for that — it needs both
    # the macro and the include path visible at its *own* compile
    # time, not just inside bot_ui's sources. PRIVATE here would
    # silently strip all of that code back out, the same way the
    # missing macro did before this file was first fixed.
    target_include_directories(bot_ui PUBLIC ${X11_INCLUDE_DIR})
    target_link_libraries(bot_ui PUBLIC ${X11_LIBRARIES})
    target_compile_definitions(bot_ui PUBLIC BOTOS_HAS_X11=1)
    message(STATUS "BotUI: X11 found — native window backend enabled")
else()
    message(STATUS "BotUI: X11 not found — using framebuffer stubs")
endif()

set_target_properties(bot_ui PROPERTIES
    OUTPUT_NAME "bot_ui"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Optional FreeType dependency — real, anti-aliased TTF glyph rendering
# instead of the built-in fixed 5x7 bitmap font. widget.c falls back to
# the bitmap font automatically (both at build time via this macro, and
# at runtime if the bundled font file can't be found/loaded), so this
# stays fully optional the same way SDL2/X11 are: PRIVATE scope is
# correct here since only widget.c's own sources check the macro.
find_package(Freetype QUIET)
if(FREETYPE_FOUND)
    target_include_directories(bot_ui PRIVATE ${FREETYPE_INCLUDE_DIRS})
    target_link_libraries(bot_ui PRIVATE ${FREETYPE_LIBRARIES})
    target_compile_definitions(bot_ui PRIVATE
        BOTOS_HAS_FREETYPE=1
        BOTOS_FONT_DEV_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/DejaVuSansMono.ttf"
    )
    message(STATUS "BotUI: FreeType found — anti-aliased TTF text enabled")
else()
    message(STATUS "BotUI: FreeType not found — using bitmap font")
endif()

install(TARGETS bot_ui EXPORT BotOSTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/botos)
install(DIRECTORY assets/ DESTINATION ${CMAKE_INSTALL_DATADIR}/botos)
