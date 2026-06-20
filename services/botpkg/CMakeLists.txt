# ============================================================
# BotOS Core — BotPkg (Package Manager)
# ============================================================

add_executable(botpkg
    src/botpkg.c
    src/manifest.c
    src/resolver.c
)

target_include_directories(botpkg
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(botpkg
    PRIVATE
        bot
        bot_net
        bot_log
)

set_target_properties(botpkg PROPERTIES
    OUTPUT_NAME "botpkg"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

install(TARGETS botpkg RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
