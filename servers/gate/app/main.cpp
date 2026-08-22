// GateServer.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include "CServer.h"
#include "ConfigMgr.h"
#include <hiredis/hiredis.h>


int main()
{
    try
    {
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
        net::io_context ioc{ 1 };
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](const boost::system::error_code& error, int signal_number) {

            if (error) {
                return;
            }
            ioc.stop();
            });
        std::make_shared<CServer>(ioc, gate_host, gate_port)->do_accept();
		std::cout << "GateServer is listening on " << gate_host << ':' << gate_port << std::endl;
        ioc.run();
    }
    catch (std::exception const& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
