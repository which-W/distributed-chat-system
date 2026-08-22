#include "IOContextPooL.h"
#include <iostream>
using namespace std;
IOContextPool::IOContextPool(std::size_t size) : _size(size), _ioContexts(size), _works(size), _nextIOContext(0) {
	//将ioContext和work相绑定防止run后直接退出
	for (std::size_t i = 0; i < size; i++) {
		_works[i] = std::make_unique<Work>(_ioContexts[i].get_executor());
	}

	//遍历多个ioservice,创建多个线程，每个线程内部启动ioservice
	for (std::size_t i = 0; i < _ioContexts.size(); i++) {
		_threads.emplace_back([this, i]() {
			_ioContexts[i].run();
			});
	}

}

IOContextPool::~IOContextPool() {
	std::cout << "AsioIOServicePool destruct" << std::endl;
}

boost::asio::io_context& IOContextPool::GetIOContext() {
	auto& service = _ioContexts[_nextIOContext++];
	if (_nextIOContext == _ioContexts.size()) {
		_nextIOContext = 0;

	}
	return service;
}

void IOContextPool::Stop() {
	for (std::size_t i = 0; i < _works.size(); ++i) {
		_works[i].reset();
		_ioContexts[i].stop();
	}

	for (auto& t : _threads) {
		t.join();
	}
}
