#pragma once
#include <chrono>
#include "const.h"
#include "LogicSystem.h"
class HttpConnection : public std::enable_shared_from_this<HttpConnection>
{
	friend class LogicSystem;
public:
	HttpConnection(boost::asio::io_context & ioc);
	void start();
	tcp::socket& GetSocket();
private:
	void CheckDeadline();
	void WriteResponse();
	void HandleReq();
	bool PreParseGetParam();
	tcp::socket _socket;
	beast::flat_buffer _buffer{ 8192 }; // 8KB buffer
	http::request<http::dynamic_body> _req; // HTTP request
	http::response<http::dynamic_body> _res; // HTTP response
	net::steady_timer _deadline{
		_socket.get_executor(),std::chrono::seconds(30)}; // Deadline timer for timeout
	std::string _get_url; // URL from the GET request
	std::unordered_map<std::string, std::string> _get_params; // Parsed GET parameters
};

// 将十六进制字符转换为数字
static bool FromHex(unsigned char x, unsigned char& value)
{
	if (x >= 'A' && x <= 'F') value = x - 'A' + 10;
	else if (x >= 'a' && x <= 'f') value = x - 'a' + 10;
	else if (x >= '0' && x <= '9') value = x - '0';
	else return false;
	return true;
}

//char 转为16进制
static unsigned char ToHex(unsigned char x)
{
	return  x > 9 ? x + 55 : x + 48;
}

//URLEncode
static std::string UrlEncode(const std::string& str)
{
	std::string strTemp = "";
	size_t length = str.length();
	for (size_t i = 0; i < length; i++)
	{
		//判断是否仅有数字和字母构成
		if (isalnum((unsigned char)str[i]) ||
			(str[i] == '-') ||
			(str[i] == '_') ||
			(str[i] == '.') ||
			(str[i] == '~'))
			strTemp += str[i];
		else if (str[i] == ' ') //为空字符
			strTemp += "+";
		else
		{
			//其他字符需要提前加%并且高四位和低四位分别转为16进制
			strTemp += '%';
			strTemp += ToHex((unsigned char)str[i] >> 4);
			strTemp += ToHex((unsigned char)str[i] & 0x0F);
		}
	}
	return strTemp;
}

//URLDecode
static bool UrlDecode(const std::string& str, std::string& decoded)
{
	decoded.clear();
	decoded.reserve(str.size());
	size_t length = str.length();
	for (size_t i = 0; i < length; i++)
	{
		//还原+为空
		if (str[i] == '+') decoded += ' ';
		//遇到%将后面的两个字符从16进制转为char再拼接
		else if (str[i] == '%')
		{
			// 网络输入不能依赖 assert；短转义或非十六进制字符必须显式失败。
			if (i + 2 >= length) return false;
			unsigned char high = 0;
			unsigned char low = 0;
			if (!FromHex(static_cast<unsigned char>(str[i + 1]), high)
				|| !FromHex(static_cast<unsigned char>(str[i + 2]), low)) return false;
			decoded += static_cast<char>(high * 16 + low);
			i += 2;
		}
		else decoded += str[i];
	}
	return true;
}
