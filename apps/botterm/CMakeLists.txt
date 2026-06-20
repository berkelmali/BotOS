# BotTerm — Graphical Terminal Emulator
add_executable(botterm src/botterm.c)
target_link_libraries(botterm PRIVATE bot_ui bot bot_log)
set_target_properties(botterm PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
install(TARGETS botterm RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
