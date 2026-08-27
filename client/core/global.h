#pragma once
#include<functional>
#include"qstyle.h"
#include<QWidget>
#include<QRegularExpression>
#include<memory>
#include<iostream>
#include <QByteArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QNetworkReply>
#include <Qstring>
#include <QDIR>
#include <QSettings>
#include <QString>
#include <iostream>
//用来刷新Qss的
extern std::function<void(QWidget*)> repolish;

extern QString gate_url_prefix;
extern bool allow_insecure_transport;

enum Req
{
	ID_GET_VERIFT_CODE = 1001,//获取验证码
	ID_REQ_USER = 1002,//用户注册
	ID_RESET_PWD =1003,//密码修改
	ID_LOGIN_USER = 1004,//用户注册
	ID_CHAT_LOGIN = 1005, //登陆聊天服务器
	ID_CHAT_LOGIN_RSP = 1006, //登陆聊天服务器回包
	ID_SEARCH_USER_REQ = 1007, //用户搜索请求
	ID_SEARCH_USER_RSP = 1008, //搜索用户回包
	ID_ADD_FRIEND_REQ = 1009,  //添加好友申请
	ID_ADD_FRIEND_RSP = 1010, //申请添加好友回复
	ID_NOTIFY_ADD_FRIEND_REQ = 1011,  //通知用户添加好友申请
	ID_AUTH_FRIEND_REQ = 1013,  //认证好友请求
	ID_AUTH_FRIEND_RSP = 1014,  //认证好友回复
	ID_NOTIFY_AUTH_FRIEND_REQ = 1015, //通知用户认证好友申请
	ID_TEXT_CHAT_MSG_REQ = 1017,  //文本聊天信息请求
	ID_TEXT_CHAT_MSG_RSP = 1018,  //文本聊天信息回复
	ID_NOTIFY_TEXT_CHAT_MSG_REQ = 1019, //通知用户文本聊天信息
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
	ID_FILE_TRANSFER_CANCEL = 1035,
};

enum Modules
{
	MOD_REGISTER = 1,//注册模块
	RESETMOD = 2,//修改模块
	LODINMOD = 3 //登录模块
};

enum ErrorCode
{
	ERR_OK = 0,//成功
	ERR_FAIL = 1,//失败
	ERR_NETWORK = 2,//网络错误
	ERR_JSON = 3,//json解析错误
};

enum TipErr {
	TIP_SUCCESS = 0,
	TIP_EMAIL_ERR = 1,
	TIP_PWD_ERR = 2,
	TIP_CONFIRM_ERR = 3,
	TIP_PWD_CONFIRM = 4,
	TIP_VARIFY_ERR = 5,
	TIP_USER_ERR = 6
};

enum ClickLbState {
	Normal = 0,
	Selected = 1
};

struct ServerInfo {
	QString Host;
	QString Port;
	QString Token;
	QString Transport;
	QString TlsServerName;
	bool AllowInsecure = false;
	int Uid;
};

enum ChatUIMode {
	SearchMode, //搜索模式
	ChatMode,   //聊天模式
	ConTactMode, //联系人模式
};


//自定义QListWidgetItem的几种类型
enum ListItemType {
	CHAT_USER_ITEM, //聊天用户
	CONTACT_USER_ITEM, //联系人用户
	SEARCH_USER_ITEM, //搜索到的用户
	ADD_USER_TIP_ITEM, //提示添加用户
	INVALID_ITEM,  //不可点击条目
	GROUP_TIP_ITEM, //分组提示条目
	LINE_ITEM,  //分割线
	APPLY_FRIEND_ITEM, //好友申请
};

enum class ChatRole //聊天角色枚举
{
	Self,
	Other
};

struct MsgInfo {
	QString msgFlag;//"text,image,file"
	QString content;//表示文件和图像的url,文本信息
	QPixmap pixmap;//文件和图片的缩略图
};

//申请好友标签输入框最低长度
const int MIN_APPLY_LABEL_ED_LEN = 40;

const QString add_prefix = "添加标签 ";

const int  tip_offset = 5;

extern std::vector<QString> strs;

extern std::vector<QString> heads;

extern std::vector<QString> names;

#define CHAT_COUNT_PER_PAGE 10
