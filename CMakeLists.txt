cmake_minimum_required(VERSION 3.24)

project(ChatProject VERSION 1.0.0 LANGUAGES CXX)

option(CHAT_BUILD_SERVERS "Build Gate, Status and Chat server executables" ON)
option(CHAT_BUILD_CLIENT "Build the Qt desktop client" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(MSVC)
    add_compile_options(/utf-8 /W3)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

function(chat_copy_runtime_config target_name config_name output_name)
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target_name}>/config"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/config/${config_name}"
            "$<TARGET_FILE_DIR:${target_name}>/config/${output_name}"
        VERBATIM)
endfunction()

if(CHAT_BUILD_SERVERS)
    find_package(Threads REQUIRED)
    find_package(Boost 1.74 REQUIRED COMPONENTS filesystem system)
    find_package(Protobuf CONFIG REQUIRED)
    find_package(gRPC CONFIG REQUIRED)
    find_package(jsoncpp CONFIG REQUIRED)
    find_package(hiredis CONFIG REQUIRED)
    find_package(MySQLConnectorCPP REQUIRED)

    if(TARGET JsonCpp::JsonCpp)
        set(CHAT_JSONCPP_TARGET JsonCpp::JsonCpp)
    elseif(TARGET jsoncpp_lib)
        set(CHAT_JSONCPP_TARGET jsoncpp_lib)
    elseif(TARGET jsoncpp_static)
        set(CHAT_JSONCPP_TARGET jsoncpp_static)
    else()
        message(FATAL_ERROR "A supported jsoncpp CMake target was not found")
    endif()

    if(TARGET hiredis::hiredis)
        set(CHAT_HIREDIS_TARGET hiredis::hiredis)
    elseif(TARGET hiredis)
        set(CHAT_HIREDIS_TARGET hiredis)
    else()
        message(FATAL_ERROR "A supported hiredis CMake target was not found")
    endif()

    set(CHAT_PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/proto/message.proto")
    set(CHAT_PROTO_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(CHAT_PROTO_SOURCES
        "${CHAT_PROTO_OUTPUT_DIR}/message.pb.cc"
        "${CHAT_PROTO_OUTPUT_DIR}/message.grpc.pb.cc")
    set(CHAT_PROTO_HEADERS
        "${CHAT_PROTO_OUTPUT_DIR}/message.pb.h"
        "${CHAT_PROTO_OUTPUT_DIR}/message.grpc.pb.h")

    add_custom_command(
        OUTPUT ${CHAT_PROTO_SOURCES} ${CHAT_PROTO_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CHAT_PROTO_OUTPUT_DIR}"
        COMMAND $<TARGET_FILE:protobuf::protoc>
            --proto_path="${CMAKE_CURRENT_SOURCE_DIR}/proto"
            --cpp_out="${CHAT_PROTO_OUTPUT_DIR}"
            --grpc_out="${CHAT_PROTO_OUTPUT_DIR}"
            --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
            "${CHAT_PROTO_FILE}"
        DEPENDS "${CHAT_PROTO_FILE}" protobuf::protoc gRPC::grpc_cpp_plugin
        VERBATIM)

    add_library(chat_proto STATIC ${CHAT_PROTO_SOURCES} ${CHAT_PROTO_HEADERS})
    target_include_directories(chat_proto PUBLIC "${CHAT_PROTO_OUTPUT_DIR}")
    target_link_libraries(chat_proto PUBLIC protobuf::libprotobuf gRPC::grpc++)

    add_library(chat_server_dependencies INTERFACE)
    target_link_libraries(chat_server_dependencies INTERFACE
        chat_proto
        Boost::filesystem
        Boost::system
        Threads::Threads
        ${CHAT_JSONCPP_TARGET}
        ${CHAT_HIREDIS_TARGET}
        MySQLConnectorCPP::jdbc)

    set(GATE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/GateServer/GateServer")
    add_executable(gate_server
        "${GATE_DIR}/GateServer.cpp"
        "${GATE_DIR}/ConfigMgr.cpp"
        "${GATE_DIR}/CServer.cpp"
        "${GATE_DIR}/HttpConnection.cpp"
        "${GATE_DIR}/IOContextPooL.cpp"
        "${GATE_DIR}/LogicSystem.cpp"
        "${GATE_DIR}/MysqlDao.cpp"
        "${GATE_DIR}/MysqlMgr.cpp"
        "${GATE_DIR}/RedisMgr.cpp"
        "${GATE_DIR}/StatusGrpcClient.cpp"
        "${GATE_DIR}/VerifyGrpcClient.cpp")
    target_include_directories(gate_server PRIVATE "${GATE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/common")
    target_link_libraries(gate_server PRIVATE chat_server_dependencies)
    chat_copy_runtime_config(gate_server "gate.ini" "gate.ini")

    set(STATUS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/StatusGrpcServer/StatusGrpcServer")
    add_executable(status_server
        "${STATUS_DIR}/StatusGrpcServer.cpp"
        "${STATUS_DIR}/AsioIOServicePool.cpp"
        "${STATUS_DIR}/ChatGrpcClient.cpp"
        "${STATUS_DIR}/ConfigMgr.cpp"
        "${STATUS_DIR}/DistLock.cpp"
        "${STATUS_DIR}/MysqlDao.cpp"
        "${STATUS_DIR}/MysqlMgr.cpp"
        "${STATUS_DIR}/RedisMgr.cpp"
        "${STATUS_DIR}/StatusServiceImpl.cpp")
    target_include_directories(status_server PRIVATE "${STATUS_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/common")
    target_link_libraries(status_server PRIVATE chat_server_dependencies)
    chat_copy_runtime_config(status_server "status.ini" "status.ini")

    set(CHAT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ChatServer/ChatServer")
    add_executable(chat_server
        "${CHAT_DIR}/ChatServer.cpp"
        "${CHAT_DIR}/AsioIOServicePool.cpp"
        "${CHAT_DIR}/ChatGrpcClient.cpp"
        "${CHAT_DIR}/ChatServiceImp.cpp"
        "${CHAT_DIR}/ConfigMgr.cpp"
        "${CHAT_DIR}/CServer.cpp"
        "${CHAT_DIR}/CSession.cpp"
        "${CHAT_DIR}/DistLock.cpp"
        "${CHAT_DIR}/FileSystem.cpp"
        "${CHAT_DIR}/FileWorker.cpp"
        "${CHAT_DIR}/LogicSystem.cpp"
        "${CHAT_DIR}/LogicWorker.cpp"
        "${CHAT_DIR}/MsgNode.cpp"
        "${CHAT_DIR}/MysqlDao.cpp"
        "${CHAT_DIR}/MysqlMgr.cpp"
        "${CHAT_DIR}/RedisMgr.cpp"
        "${CHAT_DIR}/StatusGrpcClient.cpp"
        "${CHAT_DIR}/UserMgr.cpp")
    target_include_directories(chat_server PRIVATE "${CHAT_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/common")
    target_link_libraries(chat_server PRIVATE chat_server_dependencies)
    chat_copy_runtime_config(chat_server "chatserver1.ini" "chatserver1.ini")
    chat_copy_runtime_config(chat_server "chatserver2.ini" "chatserver2.ini")

    install(TARGETS gate_server status_server chat_server RUNTIME DESTINATION bin)
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/config/gate.ini"
        "${CMAKE_CURRENT_SOURCE_DIR}/config/status.ini"
        "${CMAKE_CURRENT_SOURCE_DIR}/config/chatserver1.ini"
        "${CMAKE_CURRENT_SOURCE_DIR}/config/chatserver2.ini"
        DESTINATION config)
endif()

if(CHAT_BUILD_CLIENT)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTORCC ON)

    find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Network)

    set(CLIENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Chat_Client/Chat_Client")
    file(GLOB CLIENT_SOURCES CONFIGURE_DEPENDS "${CLIENT_DIR}/*.cpp")
    file(GLOB CLIENT_HEADERS CONFIGURE_DEPENDS "${CLIENT_DIR}/*.h")
    file(GLOB CLIENT_FORMS CONFIGURE_DEPENDS "${CLIENT_DIR}/*.ui")
    file(GLOB CLIENT_RESOURCES CONFIGURE_DEPENDS "${CLIENT_DIR}/*.qrc")

    add_executable(chat_client WIN32
        ${CLIENT_SOURCES}
        ${CLIENT_HEADERS}
        ${CLIENT_FORMS}
        ${CLIENT_RESOURCES})
    target_include_directories(chat_client PRIVATE "${CLIENT_DIR}")
    target_link_libraries(chat_client PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Network)

    add_custom_command(TARGET chat_client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/config/client.ini"
            "$<TARGET_FILE_DIR:chat_client>/config.ini"
        VERBATIM)
    install(TARGETS chat_client RUNTIME DESTINATION bin BUNDLE DESTINATION bin)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/config/client.ini" DESTINATION bin RENAME config.ini)
endif()

if(CHAT_BUILD_SERVERS)
    if(WIN32)
        add_custom_target(run_all
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-all.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
            DEPENDS gate_server status_server chat_server
            USES_TERMINAL)
        add_custom_target(stop_all
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/stop-all.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
            USES_TERMINAL)
    else()
        add_custom_target(run_all
            COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-all.sh" "${CMAKE_BINARY_DIR}"
            DEPENDS gate_server status_server chat_server
            USES_TERMINAL)
        add_custom_target(stop_all
            COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/scripts/stop-all.sh" "${CMAKE_BINARY_DIR}"
            USES_TERMINAL)
    endif()
endif()
