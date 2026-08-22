#include "IOThreadPool.h"

AsioIOThreadPool::AsioIOThreadPool(int threadNum):_threadNum(threadNum), _work(new boost::asio::io_context::work(_service)) {
	for (int i = 0; i < threadNum; ++i) {
		_threads.emplace_back([this]() {
			_service.run();
		});
	}
}

boost::asio::io_context& AsioIOThreadPool::GetIOService() {
	return _service;
}

void AsioIOThreadPool::Stop() {
	_service.stop();
	_work.reset();
	for (auto &t : _threads)
	{
		t.join();
	}
}
