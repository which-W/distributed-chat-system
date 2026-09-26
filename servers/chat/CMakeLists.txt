add_executable(chat_server
    app/main.cpp
    config/ConfigMgr.cpp
    network/CServer.cpp
    network/CSession.cpp
    network/MsgNode.cpp
    rpc/ChatGrpcClient.cpp
    rpc/ChatServiceImp.cpp
    rpc/StatusGrpcClient.cpp
    runtime/AsioIOServicePool.cpp
    runtime/IOThreadPool.cpp
    service/LogicSystem.cpp
    service/UserMgr.cpp
    storage/DistLock.cpp
    storage/MysqlDao.cpp
    storage/MysqlMgr.cpp
    storage/RedisMgr.cpp)

target_include_directories(chat_server PRIVATE
    config core network rpc runtime service storage
    "${PROJECT_SOURCE_DIR}/common")
target_link_libraries(chat_server PRIVATE chat_server_dependencies chat_security chat_observability)

chat_copy_runtime_config(chat_server "chatserver1.ini" "chatserver1.ini")
chat_copy_runtime_config(chat_server "chatserver2.ini" "chatserver2.ini")
