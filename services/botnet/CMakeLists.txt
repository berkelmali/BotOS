# ============================================================
# BotOS Core — BotNet (Network Stack)
# ============================================================

find_package(OpenSSL REQUIRED)

add_library(bot_net STATIC
    src/botnet.c
    src/http.c
    src/tcp.c
)

target_include_directories(bot_net
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(bot_net
    PUBLIC
        bot_log
    PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
)

set_target_properties(bot_net PROPERTIES
    OUTPUT_NAME "bot_net"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

install(TARGETS bot_net EXPORT BotOSTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/botos)
