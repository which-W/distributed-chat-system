// GateServer.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "CServer.h"
#include "ChatLogger.h"
#include "ConfigMgr.h"
#include <hiredis/hiredis.h>
#include <iostream>

int main() {
    chat::observability::initialize("gate_server");
    try {
        auto& GCPCfgMgr = ConfigMgr::ins();
        std::string gate_host = GCPCfgMgr["GateServer"]["Host"];
        if (gate_host.empty()) {
            gate_host = "127.0.0.1";
        }
        const std::string get_port_str = GCPCfgMgr["GateServer"]["Port"];
        const int configured_port = std::stoi(get_port_str);
        if (configured_port < 1 || configured_port > 65535) {
            throw std::out_of_range("GateServer.Port must be between 1 and 65535");
        }
        const auto gate_port = static_cast<unsigned short>(configured_port);
        net::io_context ioc{1};
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](const boost::system::error_code& error, int signal_number) {
            if (error) {
                return;
            }
            chat::observability::log(chat::observability::Level::Info, "server.shutdown_requested",
                                     "shutdown signal received",
                                     {{"signal", std::int64_t(signal_number)}});
            ioc.stop();
        });
        std::make_shared<CServer>(ioc, gate_host, gate_port)->do_accept();
        chat::observability::log(chat::observability::Level::Info, "server.started",
                                 "gate server is listening",
                                 {{"host", gate_host}, {"port", std::int64_t(gate_port)}});
        ioc.run();
        chat::observability::log(chat::observability::Level::Info, "server.stopped",
                                 "gate server stopped");
    } catch (std::exception const& e) {
        chat::observability::log(chat::observability::Level::Error, "server.start_failed",
                                 "gate server failed", {{"error", std::string(e.what())}});
        chat::observability::shutdown();
        return EXIT_FAILURE;
    }
    chat::observability::shutdown();
    return EXIT_SUCCESS;
}
