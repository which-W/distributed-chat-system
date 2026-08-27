#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include "const.h"
#include <iostream>
#include <boost/asio.hpp>
using namespace std;
using boost::asio::ip::tcp;
class MsgNode
{
public:
	MsgNode(std::size_t max_len) :_total_len(max_len), _cur_len(0) {

		_data = new char[_total_len + 1]();
		_data[_total_len] = '\0';
	}

	~MsgNode() {
		std::cout << "destruct MsgNode" << endl;
		delete[] _data;
	}

	void Clear() {
		::memset(_data, 0, _total_len);
		_cur_len = 0;
	}

	std::size_t _cur_len;
	std::size_t _total_len;
	char* _data;
};

class RecvNode :public MsgNode {
public:
	RecvNode(std::size_t max_len, short msg_id);
	short GetRecMsgNodeID();
private:
	short _msg_id;
};

class SendNode :public MsgNode {
public:
	SendNode(const char* msg, std::size_t max_len, short msg_id);
private:
	short _msg_id;
};
