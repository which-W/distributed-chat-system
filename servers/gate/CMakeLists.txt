add_executable(gate_server
    app/main.cpp
    config/ConfigMgr.cpp
    network/CServer.cpp
    network/HttpConnection.cpp
    network/IOContextPooL.cpp
    rpc/StatusGrpcClient.cpp
    rpc/VerifyGrpcClient.cpp
    service/LogicSystem.cpp
    storage/MysqlDao.cpp
    storage/MysqlMgr.cpp
    storage/RedisMgr.cpp)

target_include_directories(gate_server PRIVATE
    config core network rpc service storage
    "${PROJECT_SOURCE_DIR}/common")
target_link_libraries(gate_server PRIVATE chat_server_dependencies chat_security)

chat_copy_runtime_config(gate_server "gate.ini" "gate.ini")
