#include "IOThreadPool.h"

AsioIOThreadPool::AsioIOThreadPool(int threadNum):_threadNum(threadNum), _work(std::make_unique<Work>(_service.get_executor())) {
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
