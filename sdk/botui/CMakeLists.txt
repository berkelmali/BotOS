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
    target_compile_definitions(bot_ui PRIVATE BOTOS_HAS_SDL2=1)
    message(STATUS "BotUI: SDL2 found — hardware-accelerated rendering enabled")
else()
    message(STATUS "BotUI: SDL2 not found — using framebuffer stubs")
endif()

set_target_properties(bot_ui PROPERTIES
    OUTPUT_NAME "bot_ui"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

install(TARGETS bot_ui EXPORT BotOSTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/botos)
