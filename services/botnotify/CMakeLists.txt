# ============================================================
# BotOS Core — BotNotify (Desktop Notifications)
# ============================================================

add_library(bot_notify STATIC
    src/bot_notify.c
)

target_include_directories(bot_notify
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

set_target_properties(bot_notify PROPERTIES
    OUTPUT_NAME "bot_notify"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

add_executable(botnotify
    src/botnotify_cli.c
)
target_link_libraries(botnotify PRIVATE bot_notify)
set_target_properties(botnotify PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

install(TARGETS bot_notify EXPORT BotOSTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(TARGETS botnotify RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/botos)
