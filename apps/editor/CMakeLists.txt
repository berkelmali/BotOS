# Editor — Text Editor
add_executable(editor src/editor.c)
target_link_libraries(editor PRIVATE bot_ui bot bot_log)
set_target_properties(editor PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
install(TARGETS editor RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
