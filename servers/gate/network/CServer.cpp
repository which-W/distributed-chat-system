#include "CServer.h"
#include "IOContextPooL.h"
CServer::CServer(net::io_context& ioc, const std::string& listen_host, unsigned short port)
    : _acceptor(ioc, tcp::endpoint(net::ip::make_address(listen_host), port)), _ioc(ioc) {}

void CServer::do_accept() {
    if (_stopping || !_acceptor.is_open())
        return;
    auto self = shared_from_this();
    auto& ioc = IOContextPool::GetInstance()->GetIOContext();
    std::shared_ptr<HttpConnection> conn = std::make_shared<HttpConnection>(ioc);
    _acceptor.async_accept(conn->GetSocket(), [self, conn](beast::error_code ec) {
        try {
            // 出错放弃链接并监听其他链接
            if (ec) {
                if (!self->_stopping)
                    self->do_accept();
                return;
            }
            // 链接成功
            conn->start();
            // 继续监听
            self->do_accept();
        } catch (std::exception& ec) {
        }
    });
}

void CServer::Stop() {
    if (_stopping)
        return;
    _stopping = true;
    beast::error_code ignored;
    _acceptor.cancel(ignored);
    _acceptor.close(ignored);
}
