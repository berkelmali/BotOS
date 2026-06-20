# BotPkg GUI — Package Manager
add_executable(botpkg_gui src/botpkg_gui.c)
target_link_libraries(botpkg_gui PRIVATE bot_ui bot bot_log)
set_target_properties(botpkg_gui PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
install(TARGETS botpkg_gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
