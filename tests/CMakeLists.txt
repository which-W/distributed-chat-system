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

if(CHAT_BUILD_SERVERS)
    add_executable(mysql_pool_security_test mysql_pool_security_test.cpp)
    target_include_directories(mysql_pool_security_test PRIVATE "${PROJECT_SOURCE_DIR}/common")
    target_link_libraries(mysql_pool_security_test PRIVATE chat_server_dependencies)
    add_test(NAME mysql_pool_security COMMAND mysql_pool_security_test)

    add_executable(redis_pool_security_test redis_pool_security_test.cpp)
    target_include_directories(redis_pool_security_test PRIVATE "${PROJECT_SOURCE_DIR}/common")
    target_link_libraries(redis_pool_security_test PRIVATE chat_server_dependencies)
    add_test(NAME redis_pool_security COMMAND redis_pool_security_test)

    add_executable(friend_accept_transaction_test friend_accept_transaction_test.cpp)
    target_include_directories(friend_accept_transaction_test PRIVATE "${PROJECT_SOURCE_DIR}/common")
    add_test(NAME friend_accept_transaction COMMAND friend_accept_transaction_test)

    add_executable(schema_security_test schema_security_test.cpp)
    target_compile_definitions(schema_security_test PRIVATE
        CHAT_SCHEMA_FILE="${PROJECT_SOURCE_DIR}/database/schema.sql")
    add_test(NAME schema_security COMMAND schema_security_test)

    add_executable(encrypted_file_store_test encrypted_file_store_test.cpp
        "${PROJECT_SOURCE_DIR}/servers/chat/files/EncryptedFileStore.cpp")
    target_include_directories(encrypted_file_store_test PRIVATE
        "${PROJECT_SOURCE_DIR}/common" "${PROJECT_SOURCE_DIR}/servers/chat/files")
    target_link_libraries(encrypted_file_store_test PRIVATE
        chat_server_dependencies unofficial-sodium::sodium)
    add_test(NAME encrypted_file_store COMMAND encrypted_file_store_test)
endif()

find_program(CHAT_NODE_EXECUTABLE NAMES node)
if(CHAT_NODE_EXECUTABLE)
    add_test(
        NAME varify_node_tests
        COMMAND "${CHAT_NODE_EXECUTABLE}" --test
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/VarifyServer")
endif()
