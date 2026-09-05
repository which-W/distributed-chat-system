#pragma once
#include <boost/beast/http.hpp>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <memory>
#include <iostream>
#include <unordered_map>
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include "Singleton.h"
#include <assert.h>
#include <queue>
#include <jdbc/mysql_driver.h>
#include <jdbc/mysql_connection.h>
#include <jdbc/cppconn/prepared_statement.h>
#include <jdbc/cppconn/resultset.h>
#include <jdbc/cppconn/statement.h>
#include <jdbc/cppconn/exception.h>
#include <iostream>
#include <functional>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <string>

#define MAX_LENGTH 1024*2
#define MAX_FILE_FRAME_LENGTH 60*1024
//头部总长度
#define HEAD_TOTAL_LEN 4
//头部id长度
#define HEAD_ID_LEN 2
//头部数据长度
#define HEAD_DATA_LEN 2
#define MAX_RECVQUE  1000
#define MAX_SENDQUE 1000
#define MAX_MSG_QUEUE_SIZE 3000
// 聊天 RPC 与 16 位出站帧共享同一组数量/字节预算。
#define MAX_TEXT_MESSAGES_PER_FRAME 50
#define MAX_TEXT_CONTENT_BYTES 2048
#define MAX_TEXT_MESSAGE_ID_BYTES 128
#define MAX_OUTBOUND_FRAME_BYTES 65535
// 进程级会话和预认证时间预算，避免未登录连接无限占用资源。
#define MAX_CHAT_SESSIONS 10000
#define CHAT_AUTH_TIMEOUT_SECONDS 10
#define CHAT_IDLE_TIMEOUT_SECONDS 120

//file文件
//头部总长度
#define FILE_HEAD_TOTAL_LEN 6
//头部id长度
#define FILE_HEAD_ID_LEN 2
//头部数据长度
#define FILE_HEAD_DATA 4


namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

enum ErrorCodes {
	ERROR_CODE_OK = 0, // 成功
	JSON_ERROR = 1001, //JSON解析错误
	RPC_ERROR = 1002, //RPC调用错误
	VarifyExpired = 1003,//code过期
	VarifyCodeErr = 1004,//code错误
	UserExist = 1005, // 用户已存在
	PasswdErr = 1006,    //密码错误
	EmailNotMatch = 1007,//邮箱未匹配
	PasswdUpFailed = 1008,//更新失败
	PasswdInvalid = 1009, //密码错误
	UidInvalid = 1011, //用户ID无效
	TokenInvalid = 1010,   //Token失效
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


//消息头部结构体
enum Msg_ID {
	MSG_CHAT_LOGIN = 1005, //用户登陆请求
	MSG_CHAT_LOGIN_RSP = 1006, //用户登陆回包
	ID_SEARCH_USER_REQ = 1007, //用户搜索请求
	ID_SEARCH_USER_RSP = 1008, //搜索用户回包
	ID_ADD_FRIEND_REQ = 1009, //申请添加好友请求
	ID_ADD_FRIEND_RSP = 1010, //申请添加好友回复
	ID_NOTIFY_ADD_FRIEND_REQ = 1011,  //通知用户添加好友申请
	ID_AUTH_FRIEND_REQ = 1013,  //认证好友请求
	ID_AUTH_FRIEND_RSP = 1014,  //认证好友回复
	ID_NOTIFY_AUTH_FRIEND_REQ = 1015, //通知用户认证好友申请
	ID_TEXT_CHAT_MSG_REQ = 1017, //文本聊天信息请求
	ID_TEXT_CHAT_MSG_RSP = 1018, //文本聊天信息回复
	ID_NOTIFY_TEXT_CHAT_MSG_REQ = 1019, //通知用户文本聊天信息
	ID_TEXT_CHAT_MSG_ACK = 1020, //接收方确认已处理文本消息
	ID_NOTIFY_OFF_LINE_REQ = 1021, //通知用户下线
	ID_HEART_BEAT_REQ = 1023,      //心跳请求
	ID_HEARTBEAT_RSP = 1024,       //心跳回复
	ID_UPLOAD_FILE_REQ = 1025,
	ID_UPLOAD_FILE_RSP = 1026,
	ID_UPLOAD_FILE_CHUNK_REQ = 1027,
	ID_UPLOAD_FILE_CHUNK_RSP = 1028,
	ID_UPLOAD_FILE_FINISH_REQ = 1029,
	ID_UPLOAD_FILE_FINISH_RSP = 1030,
	ID_NOTIFY_FILE_REQ = 1031,
	ID_DOWNLOAD_FILE_REQ = 1032,
	ID_DOWNLOAD_FILE_CHUNK = 1033,
	ID_DOWNLOAD_FILE_DONE = 1034,
	ID_FILE_TRANSFER_CANCEL = 1035
};

#define USERIPPREFIX  "uip_"
#define CHAT_TICKET_PREFIX "chat_ticket_"
#define CHAT_HEALTH_PREFIX "chat_health_"
#define CHAT_HEARTBEAT_INTERVAL_SECONDS 5
#define CHAT_HEARTBEAT_TTL_SECONDS 15
#define IPCOUNTPREFIX  "ipcount_"
#define USER_BASE_INFO "ubaseinfo_"
#define NAME_INFO  "nameinfo_"
#define LOCK_PREFIX "lock_"
#define USER_SESSION_PREFIX "usession_"
#define LOCK_COUNT "lockcount"

//分布式锁的持有时间
#define LOCK_TIME_OUT 10
//分布式锁的重试时间
#define ACQUIRE_TIME_OUT 5

//4个逻辑工作者
#define LOGIC_WORKER_COUNT 4
//4个文件工作者
#define FILE_WORKER_COUNT 4

inline bool IsFileTransferMessage(std::uint16_t id) {
	return id >= ID_UPLOAD_FILE_REQ && id <= ID_FILE_TRANSFER_CANCEL;
}

inline bool IsClientRequestMessage(std::uint16_t id) {
	switch (id) {
	case MSG_CHAT_LOGIN:
	case ID_SEARCH_USER_REQ:
	case ID_ADD_FRIEND_REQ:
	case ID_AUTH_FRIEND_REQ:
	case ID_TEXT_CHAT_MSG_REQ:
	case ID_TEXT_CHAT_MSG_ACK:
	case ID_HEART_BEAT_REQ:
	case ID_UPLOAD_FILE_REQ:
	case ID_UPLOAD_FILE_CHUNK_REQ:
	case ID_UPLOAD_FILE_FINISH_REQ:
	case ID_DOWNLOAD_FILE_REQ:
	case ID_DOWNLOAD_FILE_DONE:
	case ID_FILE_TRANSFER_CANCEL:
		return true;
	default:
		return false;
	}
}
