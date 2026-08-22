#pragma once
#include <boost/asio.hpp>
#include "Singleton.h"
class AsioIOThreadPool :public Singleton<AsioIOThreadPool>
{
public:
	friend class Singleton<AsioIOThreadPool>;
	~AsioIOThreadPool() {}
	AsioIOThreadPool& operator=(const AsioIOThreadPool&) = delete;
	AsioIOThreadPool(const AsioIOThreadPool&) = delete;
	boost::asio::io_context& GetIOService();
	void Stop();
private:
	AsioIOThreadPool(int threadNum = std::thread::hardware_concurrency());
	boost::asio::io_context _service;
	using Work = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
	std::unique_ptr<Work> _work;
	std::vector<std::thread> _threads;
	int _threadNum;
};
