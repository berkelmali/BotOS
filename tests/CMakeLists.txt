# ============================================================
# BotOS Core — Test Suite
# ============================================================

# Simple test runner using CTest
add_executable(test_vfs   test_vfs.c)
add_executable(test_shell test_shell.c)
add_executable(test_ipc   test_ipc.c)
add_executable(test_net   test_net.c)
add_executable(test_botpkg test_botpkg.c)

target_link_libraries(test_vfs   PRIVATE bot_vfs bot_ipc)
target_link_libraries(test_shell PRIVATE bot_parser)
target_link_libraries(test_ipc   PRIVATE bot_ipc)
target_link_libraries(test_net   PRIVATE bot_net bot_log)
target_link_libraries(test_botpkg PRIVATE bot_pkg_core)

# Include paths for test access to internal headers
target_include_directories(test_vfs   PRIVATE ${CMAKE_SOURCE_DIR}/core/vfs/include)
target_include_directories(test_shell PRIVATE ${CMAKE_SOURCE_DIR}/shell/include)
target_include_directories(test_ipc   PRIVATE ${CMAKE_SOURCE_DIR}/core/ipc/include)
target_include_directories(test_net   PRIVATE ${CMAKE_SOURCE_DIR}/services/botnet/include)
target_include_directories(test_botpkg PRIVATE ${CMAKE_SOURCE_DIR}/services/botpkg/include)

set_target_properties(test_vfs test_shell test_ipc test_net test_botpkg PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

# Register with CTest
add_test(NAME vfs_tests   COMMAND test_vfs)
add_test(NAME shell_tests COMMAND test_shell)
add_test(NAME ipc_tests   COMMAND test_ipc)
add_test(NAME net_tests   COMMAND test_net)
add_test(NAME botpkg_tests COMMAND test_botpkg)
