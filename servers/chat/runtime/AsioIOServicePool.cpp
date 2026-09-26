#include "AsioIOServicePool.h"
#include "ChatLogger.h"
#include <iostream>
using namespace std;
AsioIOServicePool::AsioIOServicePool(std::size_t size)
    : _size(std::max<std::size_t>(1, size)), _ioServces(_size), _works(_size), _nextIOService(0) {
    for (std::size_t i = 0; i < _size; i++) {
        _works[i] = std::make_unique<Work>(_ioServces[i].get_executor());
    }

    // 遍历多个ioservice,创建多个线程，每个线程内部启动ioservice
    for (std::size_t i = 0; i < _ioServces.size(); i++) {
        _threads.emplace_back([this, i]() { _ioServces[i].run(); });
    }
}

AsioIOServicePool::~AsioIOServicePool() {
    Stop();
    chat::observability::stream(chat::observability::Level::Info)
        << "AsioIOServicePool destruct" << std::endl;
}

boost::asio::io_context& AsioIOServicePool::GetIOServer() {
    auto& service = _ioServces[_nextIOService++];
    if (_nextIOService == _ioServces.size()) {
        _nextIOService = 0;
    }
    return service;
}

void AsioIOServicePool::Stop() {
    // 信号回调、异常清理和析构都可能触发停止，因此重复调用必须安全。
    for (auto& work : _works) {
        if (!work)
            continue;
        static_cast<boost::asio::io_context&>(work->get_executor().context()).stop();
        work.reset();
    }

    for (auto& t : _threads) {
        if (t.joinable())
            t.join();
    }
}
