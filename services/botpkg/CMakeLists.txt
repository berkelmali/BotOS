# ============================================================
# BotOS Core — BotPkg (Package Manager)
# ============================================================

# bot_pkg_core: the parts of BotPkg with no OpenSSL/bot_net dependency
# (manifest parsing, dependency resolution, checksums) split out so
# tests/test_botpkg.c can link against real botpkg logic without
# needing bot_net — that keeps this test buildable even in toolchains
# without libssl-dev, the same environment gap that makes bot_net
# itself and test_net optional elsewhere in this project.
add_library(bot_pkg_core STATIC
    src/manifest.c
    src/resolver.c
    src/sha256.c
)
target_include_directories(bot_pkg_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(bot_pkg_core PRIVATE bot_log)

add_executable(botpkg
    src/botpkg.c
)

target_include_directories(botpkg
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(botpkg
    PRIVATE
        bot_pkg_core
        bot
        bot_net
        bot_log
)

set_target_properties(botpkg PROPERTIES
    OUTPUT_NAME "botpkg"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

install(TARGETS botpkg RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
