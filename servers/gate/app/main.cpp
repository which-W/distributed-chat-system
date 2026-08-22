// GateServer.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include "CServer.h"
#include "ConfigMgr.h"
#include <hiredis/hiredis.h>


int main()
{

    auto& GCPCfgMgr = ConfigMgr::ins();
    std:: string get_port_str = GCPCfgMgr["GateServer"]["Port"];
    unsigned short gate_port = atoi(get_port_str.c_str());
    try
    {
        unsigned short port = gate_port;
        net::io_context ioc{ 1 };
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](const boost::system::error_code& error, int signal_number) {

            if (error) {
                return;
            }
            ioc.stop();
            });
        std::make_shared<CServer>(ioc, port)->do_accept();
		std::cout << "GateServer is running on port " << port << std::endl;
        ioc.run();
    }
    catch (std::exception const& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
