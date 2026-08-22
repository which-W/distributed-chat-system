add_executable(status_server
    app/main.cpp
    config/ConfigMgr.cpp
    rpc/ChatGrpcClient.cpp
    rpc/StatusServiceImpl.cpp
    runtime/AsioIOServicePool.cpp
    storage/DistLock.cpp
    storage/MysqlDao.cpp
    storage/MysqlMgr.cpp
    storage/RedisMgr.cpp)

target_include_directories(status_server PRIVATE
    config core rpc runtime storage
    "${PROJECT_SOURCE_DIR}/common")
target_link_libraries(status_server PRIVATE chat_server_dependencies)

chat_copy_runtime_config(status_server "status.ini" "status.ini")
