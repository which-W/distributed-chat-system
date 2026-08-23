if(TARGET chat_security)
    add_executable(password_hasher_test password_hasher_test.cpp)
    target_link_libraries(password_hasher_test PRIVATE chat_security)
    add_test(NAME password_hasher COMMAND password_hasher_test)
endif()

if(TARGET chat_server_selection)
    add_executable(chat_server_selector_test chat_server_selector_test.cpp)
    target_link_libraries(chat_server_selector_test PRIVATE chat_server_selection)
    add_test(NAME chat_server_selector COMMAND chat_server_selector_test)
endif()

find_program(CHAT_NODE_EXECUTABLE NAMES node)
if(CHAT_NODE_EXECUTABLE)
    add_test(
        NAME varify_node_tests
        COMMAND "${CHAT_NODE_EXECUTABLE}" --test
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/VarifyServer")
endif()
