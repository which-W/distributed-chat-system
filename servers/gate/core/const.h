#pragma once
#include<boost/beast/http.hpp>
#include<boost/beast.hpp>
#include<boost/asio.hpp>
#include<memory>
#include<chrono>
#include<iostream>
#include<mutex>
#include<functional>
#include<map>
#include<unordered_map>
#include<assert.h>
#include<ctype.h>
#include<json/json.h>
#include<json/reader.h>
#include<json/value.h>
#include<boost/filesystem.hpp>
#include<boost/property_tree/ptree.hpp>
#include<boost/property_tree/ini_parser.hpp>


namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;


enum ERROR_CODE
{
	ERROR_CODE_OK = 0, // 成功
	ERROR_CODE_NOT_FOUND = 404, // 未找到
	ERROR_CODE_INTERNAL_ERROR = 500, // 内部错误
	ERROR_CODE_BAD_REQUEST = 400, // 错误请求
	ERROR_CODE_UNAUTHORIZED = 401, // 未授权
	ERROR_CODE_FORBIDDEN = 403, // 禁止访问
	ERROR_CODE_SERVICE_UNAVAILABLE = 503,// 服务不可用
	JSON_ERROR = 1001, //JSON解析错误
	RPC_ERROR = 1002, //RPC调用错误
	VarifyExpired = 1003,//code过期
	VarifyCodeErr = 1004,//code错误
	UserExist = 1005, // 用户已存在
	PasswdErr = 1006,    //密码错误
	EmailNotMatch = 1007,//邮箱未匹配
	PasswdUpFailed = 1008,//更新失败
	PasswdInvalid = 1009 //密码无效
};

// Defer类
class Defer {
public:
	// 接受一个lambda表达式或者函数指针
	Defer(std::function<void()> func) : func_(func) {}

	// 析构函数中执行传入的函数
	~Defer() {
		func_();
	}

private:
	std::function<void()> func_;
};
#define CODE_HEAD "code_"
