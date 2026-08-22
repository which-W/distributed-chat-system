#include "AsioIOServicePool.h"
#include <iostream>
using namespace std;
AsioIOServicePool::AsioIOServicePool(std::size_t size ) : _size(size), _ioServces(size), _works(size), _nextIOService(0){
	for (std::size_t i = 0; i < size; i++) {
		_works[i] = std::unique_ptr<Work>(new Work(_ioServces[i]));
	}

	//遍历多个ioservice,创建多个线程，每个线程内部启动ioservice
	for (std::size_t i = 0; i < _ioServces.size(); i++) {
		_threads.emplace_back([this, i]() {
			_ioServces[i].run();
			});
	}

}

AsioIOServicePool::~AsioIOServicePool() {
	std::cout << "AsioIOServicePool destruct" << std::endl;
}

boost::asio::io_context& AsioIOServicePool::GetIOServer() {
	auto& service = _ioServces[_nextIOService++];
	if (_nextIOService == _ioServces.size()) {
		_nextIOService = 0;

	}
	return service;
}

void AsioIOServicePool::Stop() {
	for (auto & work : _works) {
		work->get_io_context().stop();
		work.reset();
	}

	for (auto& t : _threads) {
		t.join();
	}
}
