# BotPaint — Paint Application
add_executable(botpaint src/botpaint.c)
target_link_libraries(botpaint PRIVATE bot_ui bot bot_log m)
set_target_properties(botpaint PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
install(TARGETS botpaint RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
