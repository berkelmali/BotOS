# BotInfo — System Documentation and Architecture
add_executable(botinfo src/botinfo.c)
target_link_libraries(botinfo PRIVATE bot_ui bot bot_log)
set_target_properties(botinfo PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
install(TARGETS botinfo RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
