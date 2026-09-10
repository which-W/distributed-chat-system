#include "AsioIOServicePool.h"
#include "ChatLogger.h"
#include "ConfigMgr.h"
#include "GrpcTlsSupport.h"
#include "InternalRpcAuth.h"
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include "StatusServiceImpl.h"
#include "const.h"
#include <boost/asio.hpp>
#include <hiredis/hiredis.h>
#include <iostream>
#include <json/json.h>
#include <json/reader.h>
#include <json/value.h>
#include <memory>
#include <string>
#include <thread>
void RunServer() {
    auto& cfg = ConfigMgr::Inst();
    chat::internal_rpc::validate_server_configuration(
        cfg["StatusServer"]["Host"], cfg["InternalRpc"]["GateToken"], cfg["GrpcTLS"]["Mode"]);
    std::string server_address(cfg["StatusServer"]["Host"] + ":" + cfg["StatusServer"]["Port"]);
    StatusServiceImpl service;
    grpc::ServerBuilder builder;
    // 监听端口和添加服务
    builder.AddListeningPort(server_address,
                             chat::grpc_tls::server_credentials(chat::grpc_tls::from_config(cfg)));
    builder.RegisterService(&service);
    // 构建并启动gRPC服务器
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    chat::observability::log(chat::observability::Level::Info, "server.started",
                             "status server is listening", {{"address", server_address}});
    // 创建Boost.Asio的io_context
    boost::asio::io_context io_context;
    // 创建signal_set用于捕获SIGINT
    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    // 设置异步等待SIGINT信号
    signals.async_wait([&server](const boost::system::error_code& error, int signal_number) {
        if (!error) {
            chat::observability::log(chat::observability::Level::Info, "server.shutdown_requested",
                                     "shutdown signal received",
                                     {{"signal", std::int64_t(signal_number)}});
            server->Shutdown(); // 优雅地关闭服务器
        }
    });
    // 在单独的线程中运行io_context
    std::thread([&io_context]() { io_context.run(); }).detach();
    // 等待服务器关闭
    server->Wait();
    io_context.stop(); // 停止io_context
}
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    chat::observability::initialize("status_server");
    try {
        RunServer();
    } catch (std::exception const& e) {
        chat::observability::log(chat::observability::Level::Error, "server.start_failed",
                                 "status server failed", {{"error", std::string(e.what())}});
        chat::observability::shutdown();
        return EXIT_FAILURE;
    }
    chat::observability::log(chat::observability::Level::Info, "server.stopped",
                             "status server stopped");
    chat::observability::shutdown();
    return 0;
}
