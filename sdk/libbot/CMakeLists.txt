# ============================================================
# BotOS Core — libbot (Core Utility Library)
# ============================================================

add_library(bot STATIC
    src/bot_io.c
    src/bot_string.c
    src/bot_mem.c
)

target_include_directories(bot
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(bot
    PUBLIC
        bot_vfs
        bot_log
)

set_target_properties(bot PROPERTIES
    OUTPUT_NAME "bot"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

install(TARGETS bot EXPORT BotOSTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/botos)
