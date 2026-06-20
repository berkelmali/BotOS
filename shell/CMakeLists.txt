# ============================================================
# BotOS Core — BotShell (L4: Development Layer)
# ============================================================

add_library(bot_parser STATIC
    src/parser.c
)
target_include_directories(bot_parser
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(botshell
    src/botshell.c
    src/executor.c
    src/builtins.c
    src/pybridge.c
)

target_include_directories(botshell
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(botshell
    PRIVATE
        bot_parser
        bot_vfs
        bot_ipc
)

# PyBridge requires Python 3
if(BOTOS_ENABLE_PYBRIDGE)
    find_package(Python3 COMPONENTS Development)
    if(Python3_FOUND)
        target_link_libraries(botshell PRIVATE Python3::Python)
        target_compile_definitions(botshell PRIVATE BOTOS_PYBRIDGE_ENABLED=1)
        message(STATUS "BotShell: PyBridge enabled (Python ${Python3_VERSION})")
    else()
        message(WARNING "BotShell: Python3 not found — PyBridge disabled")
    endif()
endif()

set_target_properties(botshell PROPERTIES
    OUTPUT_NAME "botshell"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

install(TARGETS botshell RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
