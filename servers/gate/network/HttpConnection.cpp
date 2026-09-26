#include "HttpConnection.h"
#include "ChatLogger.h"

HttpConnection::HttpConnection(boost::asio::io_context& ioc) : _socket(ioc) {}

void HttpConnection::start() {
    _request_id = chat::observability::newRequestId();
    _started = std::chrono::steady_clock::now();
    auto self = shared_from_this();
    // 截止时间必须覆盖请求读取阶段，防止慢速请求长期占用 socket。
    _deadline.expires_after(std::chrono::seconds(30));
    CheckDeadline();
    http::async_read(
        _socket, _buffer, _req, [self](beast::error_code ec, ::std::size_t bytes_transferred) {
            try {
                if (ec) {
                    self->_deadline.cancel();
                    chat::observability::log(
                        chat::observability::Level::Warn, "http.read_failed",
                        "failed to read request",
                        {{"request_id", self->_request_id}, {"error", ec.message()}});
                    return; // 读取失败，直接返回
                }
                boost::ignore_unused(bytes_transferred);
                self->HandleReq();
            } catch (std::exception& ec) {
                self->_deadline.cancel();
                chat::observability::log(
                    chat::observability::Level::Error, "http.request_exception",
                    "request handler raised an exception",
                    {{"request_id", self->_request_id}, {"error", std::string(ec.what())}});
            }
        });
}

tcp::socket& HttpConnection::GetSocket() {
    return _socket;
}

void HttpConnection::CheckDeadline() {
    auto self = shared_from_this();
    _deadline.async_wait([self](beast::error_code ec) {
        if (!ec) {
            self->_socket.close(ec);
        }
    });
}

void HttpConnection::WriteResponse() {
    auto self = shared_from_this();
    _res.set("X-Request-Id", _request_id);
    _res.content_length(_res.body().size());
    http::async_write(_socket, _res, [self](beast::error_code ec, ::std::size_t bytes_transferred) {
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - self->_started)
                                  .count();
        chat::observability::log(
            ec ? chat::observability::Level::Warn : chat::observability::Level::Info,
            "http.request_completed", ec ? "response write failed" : "request completed",
            {{"request_id", self->_request_id},
             {"method", std::string(self->_req.method_string())},
             {"target", std::string(self->_req.target())},
             {"status", std::int64_t(self->_res.result_int())},
             {"duration_ms", std::int64_t(duration)},
             {"bytes", std::uint64_t(bytes_transferred)}});
        self->_socket.shutdown(tcp::socket::shutdown_send, ec);
        self->_deadline.cancel();
    });
}

void HttpConnection::HandleReq() {
    _res.version(_req.version());
    _res.keep_alive(false);

    if (_req.method() == http::verb::get) {
        // 处理 GET 请求
        if (!PreParseGetParam()) {
            _res.result(http::status::bad_request);
            _res.set(http::field::content_type, "text/plain");
            beast::ostream(_res.body()) << "invalid URL encoding";
            WriteResponse();
            return;
        }
        bool success = LogicSystem::GetInstance()->HandleGet(_get_url, shared_from_this());
        if (!success) {
            // 如果处理失败，返回 404 Not Found
            _res.result(http::status::not_found);
            _res.set(http::field::content_type, "text/plain");
            beast::ostream(_res.body()) << "404 Not Found\n url not found.";
            WriteResponse();
            return;
        }
        // 处理器可返回更明确的错误状态；仅保留默认的 200 状态。
        _res.set(http::field::server, "GateServer");
        WriteResponse();
        return;
    }
    if (_req.method() == http::verb::post) {
        bool success = LogicSystem::GetInstance()->HandlePost(_req.target(), shared_from_this());
        if (!success) {
            _res.result(http::status::not_found);
            _res.set(http::field::content_type, "text/plain");
            beast::ostream(_res.body()) << "url not found\r\n";
            WriteResponse();
            return;
        }

        // 保留处理器设置的状态码。
        _res.set(http::field::server, "GateServer");
        WriteResponse();
        return;
    }
    // 未支持的方法立即结束请求，避免连接只能等待截止时间被动回收。
    _res.result(http::status::method_not_allowed);
    _res.set(http::field::allow, "GET, POST");
    _res.set(http::field::content_type, "text/plain");
    beast::ostream(_res.body()) << "method not allowed";
    WriteResponse();
}

void HttpConnection::SetJsonError(http::status status, int error) {
    _res.result(status);
    _res.set(http::field::content_type, "application/json");
    Json::Value response;
    response["error"] = error;
    beast::ostream(_res.body()) << response.toStyledString();
}

bool HttpConnection::PreParseGetParam() {
    // 提取 URI
    auto uri = _req.target();
    // 查找查询字符串的开始位置（即 '?' 的位置）
    auto query_pos = uri.find('?');
    if (query_pos == std::string::npos) {
        _get_url = uri;
        return true;
    }

    _get_url = uri.substr(0, query_pos);
    std::string query_string = uri.substr(query_pos + 1);
    std::string key;
    std::string value;
    size_t pos = 0;
    while ((pos = query_string.find('&')) != std::string::npos) {
        auto pair = query_string.substr(0, pos);
        size_t eq_pos = pair.find('=');
        if (eq_pos != std::string::npos) {
            if (!UrlDecode(pair.substr(0, eq_pos), key) ||
                !UrlDecode(pair.substr(eq_pos + 1), value))
                return false;
            _get_params[key] = value;
        }
        query_string.erase(0, pos + 1);
    }
    // 处理最后一个参数对（如果没有 & 分隔符）
    if (!query_string.empty()) {
        size_t eq_pos = query_string.find('=');
        if (eq_pos != std::string::npos) {
            if (!UrlDecode(query_string.substr(0, eq_pos), key) ||
                !UrlDecode(query_string.substr(eq_pos + 1), value))
                return false;
            _get_params[key] = value;
        }
    }
    return true;
}
