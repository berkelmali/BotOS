# ============================================================
# BotOS Core — BotLog (Centralized Logging Service)
# ============================================================

add_library(bot_log STATIC
    src/botlog.c
)

target_include_directories(bot_log
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

set_target_properties(bot_log PROPERTIES
    OUTPUT_NAME "bot_log"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

install(TARGETS bot_log EXPORT BotOSTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/botos)
