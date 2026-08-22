#pragma once
#include "const.h"
#include "Singleton.h"
class IOContextPool : public Singleton<IOContextPool>
{
	friend Singleton<IOContextPool>;
public:
	using IOContext = boost::asio::io_context;
	using Work = boost::asio::io_context::work;
	using WorkPtr = std::unique_ptr<Work>;
	~IOContextPool();
	IOContextPool(const IOContextPool&) = delete;
	IOContextPool& operator = (const IOContextPool&) = delete;
	boost::asio::io_context& GetIOContext();
	void Stop();
private:
	IOContextPool(std::size_t size = std::thread::hardware_concurrency());
	std::vector<IOContext> _ioContexts;
	std::vector<WorkPtr> _works;
	std::vector<std::thread> _threads;
	std::size_t _nextIOContext;
	std::size_t _size;
};
