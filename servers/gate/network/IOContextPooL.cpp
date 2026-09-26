#include "IOContextPooL.h"
#include "ChatLogger.h"
#include <iostream>
#include <algorithm>
using namespace std;
IOContextPool::IOContextPool(std::size_t size)
    : _size(std::max<std::size_t>(1, size)), _ioContexts(_size), _works(_size), _nextIOContext(0) {
    // 将ioContext和work相绑定防止run后直接退出
    for (std::size_t i = 0; i < size; i++) {
        _works[i] = std::make_unique<Work>(_ioContexts[i].get_executor());
    }

    // 遍历多个ioservice,创建多个线程，每个线程内部启动ioservice
    for (std::size_t i = 0; i < _ioContexts.size(); i++) {
        _threads.emplace_back([this, i]() { _ioContexts[i].run(); });
    }
}

IOContextPool::~IOContextPool() {
    // 析构可能晚于显式 Stop；只 join 仍可 join 的线程。
    Stop();
    chat::observability::stream(chat::observability::Level::Info)
        << "AsioIOServicePool destruct" << std::endl;
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
        if (t.joinable())
            t.join();
    }
}
