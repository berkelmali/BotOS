# BotDesk — Window Manager
add_executable(botdesk src/botdesk.c)
target_link_libraries(botdesk PRIVATE bot_ui bot bot_log)
set_target_properties(botdesk PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
install(TARGETS botdesk RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
