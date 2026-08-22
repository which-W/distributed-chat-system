#include "LogicSystem.h"
#include <csignal>
#include <thread>
#include <mutex>
#include "AsioIOServicePool.h"
#include "CServer.h"
#include "ConfigMgr.h"
#include "ChatServiceImp.h"
#include "GrpcTlsSupport.h"
bool bstop = false;
std::condition_variable cond_quit;
std::mutex mutex_quit;

int main()
{
	auto& cfg = ConfigMgr::Inst();
	auto server_name = cfg["SelfServer"]["Name"];
	try {
		auto pool = AsioIOServicePool::GetInstance();
		//将登录数设置为0
		RedisMgr::GetInstance()->HSet(LOGIN_COUNT, server_name, "0");
		Defer derfer([server_name]() {
			RedisMgr::GetInstance()->HDel(LOGIN_COUNT, server_name);
			RedisMgr::GetInstance()->Close();
			});

		boost::asio::io_context  io_context;
		auto listen_host = cfg["SelfServer"]["ListenHost"];
		if (listen_host.empty()) {
			listen_host = "127.0.0.1";
		}
		auto port_str = cfg["SelfServer"]["Port"];
		const int configured_port = std::stoi(port_str);
		if (configured_port < 1 || configured_port > 65535) {
			throw std::out_of_range("SelfServer.Port must be between 1 and 65535");
		}
		//创建Cserver智能指针
		auto pointer_server = std::make_shared<CServer>(
			io_context, listen_host, static_cast<unsigned short>(configured_port));
		//启动定时器
		//pointer_server->StartTimer();

		//定义一个GrpcServer
		std::string server_address(cfg["SelfServer"]["Host"] + ":" + cfg["SelfServer"]["RPCPort"]);
		ChatServiceImp service;
		grpc::ServerBuilder builder;
		// 监听端口和添加服务
		builder.AddListeningPort(server_address, chat::grpc_tls::server_credentials(
			chat::grpc_tls::from_config(cfg)));
		builder.RegisterService(&service);
		service.RegisterServer(pointer_server);
		// 构建并启动gRPC服务器
		std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
		std::cout << "RPC Server listening on " << server_address << std::endl;

		//单独启动一个线程处理grpc服务
		std::thread  grpc_server_thread([&server]() {
			server->Wait();
			});


		boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
		signals.async_wait([&io_context, pool, &server](auto, auto) {
			io_context.stop();
			pool->Stop();
			server->Shutdown();
			});


		//将Cserver注册给逻辑类方便以后清除连接
		LogicSystem::GetInstance()->SetServer(pointer_server);
		io_context.run();

		grpc_server_thread.join();
		pointer_server->StopTimer();
		return 0;
	}
	catch (std::exception& e) {
		std::cerr << "Exception: " << e.what() << endl;
	}

}
