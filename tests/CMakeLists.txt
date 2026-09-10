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
    foreach(test_name IN ITEMS delivery_window keyed_executor token_bucket text_message_service)
        add_executable(${test_name}_test ${test_name}_test.cpp)
        target_include_directories(${test_name}_test PRIVATE "${PROJECT_SOURCE_DIR}/common")
        target_link_libraries(${test_name}_test PRIVATE Threads::Threads)
        add_test(NAME ${test_name} COMMAND ${test_name}_test)
    endforeach()
    add_executable(chat_logger_test chat_logger_test.cpp)
    target_include_directories(chat_logger_test PRIVATE "${PROJECT_SOURCE_DIR}/common")
    target_link_libraries(chat_logger_test PRIVATE chat_observability ${CHAT_JSONCPP_TARGET})
    add_test(NAME chat_logger COMMAND chat_logger_test)

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

	add_executable(chat_protocol_test chat_protocol_test.cpp)
	target_include_directories(chat_protocol_test PRIVATE
		"${PROJECT_SOURCE_DIR}/common" "${PROJECT_SOURCE_DIR}/servers/chat/core")
	target_link_libraries(chat_protocol_test PRIVATE chat_server_dependencies)
	add_test(NAME chat_protocol COMMAND chat_protocol_test)

    add_executable(encrypted_file_store_test encrypted_file_store_test.cpp
        "${PROJECT_SOURCE_DIR}/servers/chat/files/EncryptedFileStore.cpp")
    target_include_directories(encrypted_file_store_test PRIVATE
        "${PROJECT_SOURCE_DIR}/common" "${PROJECT_SOURCE_DIR}/servers/chat/files")
    target_link_libraries(encrypted_file_store_test PRIVATE
        chat_server_dependencies unofficial-sodium::sodium)
    add_test(NAME encrypted_file_store COMMAND encrypted_file_store_test)
endif()

find_program(CHAT_NODE_EXECUTABLE NAMES node)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND AND CHAT_BUILD_SERVERS)
    add_test(NAME benchmark_comparison
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/benchmark_comparison_test.py")
    add_test(NAME benchmark_protocol
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/benchmark_protocol_test.py"
                "$<TARGET_FILE:chat_loadgen>")
    set_tests_properties(benchmark_protocol PROPERTIES TIMEOUT 30)
endif()
if(CHAT_NODE_EXECUTABLE)
    add_test(
        NAME varify_node_tests
        COMMAND "${CHAT_NODE_EXECUTABLE}" --test
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/VarifyServer")
endif()
