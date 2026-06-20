# BotFiles — File Manager
add_executable(botfiles src/botfiles.c)
target_link_libraries(botfiles PRIVATE bot_ui bot bot_log)
set_target_properties(botfiles PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
install(TARGETS botfiles RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
