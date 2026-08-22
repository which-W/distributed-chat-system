#pragma once
#include "const.h"
#include"HttpConnection.h"

class CServer:public std::enable_shared_from_this<CServer>
{
public:
	CServer(net::io_context& ioc, unsigned short& port);
	void do_accept();

private:
	tcp::acceptor _acceptor;
	net::io_context &_ioc; //不存在拷贝构造与复制所以只能使用应用
};
