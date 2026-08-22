#include "HttpConnection.h"

HttpConnection::HttpConnection(boost::asio::io_context & ioc) :_socket(ioc)
{

}

void HttpConnection::start()
{
	auto self = shared_from_this();
	http::async_read(
		_socket,
		_buffer,
		_req,
		[self](beast::error_code ec, ::std::size_t bytes_transferred) {
		try{
			if (ec) {
				std::cout << "Error reading request: " << ec.what() << std::endl;
				return; // 读取失败，直接返回
			}
			boost::ignore_unused(bytes_transferred);
			self->HandleReq();
			self->CheckDeadline();
		}
		catch (std::exception& ec) {
			std::cout << "Exception in HttpConnection::start: " << ec.what() << std::endl;
		}


		});
}

tcp::socket& HttpConnection::GetSocket()
{
	return _socket;
}

void HttpConnection::CheckDeadline()
{
	auto self = shared_from_this();
	_deadline.async_wait([self](beast::error_code ec) {
		if (!ec) {
			self->_socket.close(ec);
		}
	});
}

void HttpConnection::WriteResponse()
{
	auto self = shared_from_this();
	_res.content_length(_res.body().size());
	http::async_write(
		_socket,
		_res,
		[self](beast::error_code ec, ::std::size_t bytes_transferred) {
			self->_socket.shutdown(tcp::socket::shutdown_send, ec);
			self->_deadline.cancel();
		});
}

void HttpConnection::HandleReq()
{
	_res.version(_req.version());
	_res.keep_alive(false);

	if (_req.method() == http::verb::get) {
		// 处理 GET 请求
		PreParseGetParam();
		bool success = LogicSystem::GetInstance()->HandleGet(_get_url, shared_from_this());
		if (!success) {
			// 如果处理失败，返回 404 Not Found
			_res.result(http::status::not_found);
			_res.set(http::field::content_type, "text/plain");
			beast::ostream(_res.body()) << "404 Not Found\n url not found.";
			WriteResponse();
			return;
		}
		// 如果处理成功，返回 200 OK
		_res.result(http::status::ok);
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

		_res.result(http::status::ok);
		_res.set(http::field::server, "GateServer");
		WriteResponse();
		return;
	}
}

void HttpConnection::PreParseGetParam() {
	// 提取 URI
	auto uri = _req.target();
	// 查找查询字符串的开始位置（即 '?' 的位置）
	auto query_pos = uri.find('?');
	if (query_pos == std::string::npos) {
		_get_url = uri;
		return;
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
			key = UrlDecode(pair.substr(0, eq_pos)); // 假设有 url_decode 函数来处理URL解码
			value = UrlDecode(pair.substr(eq_pos + 1));
			_get_params[key] = value;
		}
		query_string.erase(0, pos + 1);
	}
	// 处理最后一个参数对（如果没有 & 分隔符）
	if (!query_string.empty()) {
		size_t eq_pos = query_string.find('=');
		if (eq_pos != std::string::npos) {
			key = UrlDecode(query_string.substr(0, eq_pos));
			value = UrlDecode(query_string.substr(eq_pos + 1));
			_get_params[key] = value;
		}
	}
}
