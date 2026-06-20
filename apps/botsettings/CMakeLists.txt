# BotSettings — System Configuration
add_executable(botsettings src/botsettings.c)
target_link_libraries(botsettings PRIVATE bot_ui bot bot_log)
set_target_properties(botsettings PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
install(TARGETS botsettings RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
