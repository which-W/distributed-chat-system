add_executable(chat_loadgen chat_loadgen.cpp)
target_link_libraries(chat_loadgen PRIVATE chat_server_dependencies)

add_executable(log_benchmark log_benchmark.cpp)
target_link_libraries(log_benchmark PRIVATE LogSystem::LogSystem Threads::Threads)
