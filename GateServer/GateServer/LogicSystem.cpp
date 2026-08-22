#include "LogicSystem.h"
#include "VerifyGrpcClient.h"
#include "MysqlMgr.h"
#include "StatusGrpcClient.h"
LogicSystem::LogicSystem() {
    //测试
	RegGet("/get_test", [](std::shared_ptr<HttpConnection> conn) {
		beast::ostream(conn->_res.body()) << "Hello, World!";
		});

    //验证码逻辑
	RegPost("/post_email", [](std::shared_ptr<HttpConnection> conn) {
		std::string body_str = boost::beast::buffers_to_string(conn->_req.body().data());
		conn->_res.set(http::field::content_type, "test/json");
		Json::Value root;
		Json::Reader reader;
		Json::Value src_root;
		bool prase_success = reader.parse(body_str, src_root);
		if (!prase_success) {
			std::cout << "JSON parse error" << std::endl;
			root["error"] = ERROR_CODE::JSON_ERROR;
			std::string jsonstr = root.toStyledString();
			beast::ostream(conn->_res.body()) << jsonstr;
			return true;
		}
		if (!src_root.isMember("email")) {
			std::cout << "JSON parse error" << std::endl;
			root["error"] = ERROR_CODE::JSON_ERROR;
			std::string jsonstr = root.toStyledString();
			beast::ostream(conn->_res.body()) << jsonstr;
			return true;
		}

		auto email = src_root["email"].asString();
		GetVarifyRsp _rsp = VerifyGrpcClient::GetInstance()->GetVarifyCode(email);
		root["error"] = _rsp.error();
		root["email"] = email;
		std::string jsonstr = root.toStyledString();
		beast::ostream(conn->_res.body()) << jsonstr;
		return true;
		});

    //注册逻辑
    RegPost("/user_register", [](std::shared_ptr<HttpConnection> connection) {
        auto body_str = boost::beast::buffers_to_string(connection->_req.body().data());
        connection->_res.set(http::field::content_type, "text/json");
        Json::Value root;
        Json::Reader reader;
        Json::Value src_root;
        bool parse_success = reader.parse(body_str, src_root);
        if (!parse_success) {
            std::cout << "Failed to parse JSON data!" << std::endl;
            root["error"] = ERROR_CODE::JSON_ERROR;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_res.body()) << jsonstr;
            return true;
        }
        auto email = src_root["email"].asString();
        auto name = src_root["user"].asString();
        auto pwd = src_root["passwd"].asString();
        auto icon = src_root["icon"].asString();

        //先查找redis中email对应的验证码是否合理
        std::string  varify_code;
        bool b_get_varify = RedisMgr::GetInstance()->Get(CODE_HEAD + src_root["email"].asString(), varify_code);
        if (!b_get_varify) {
            std::cout << " get varify code expired" << std::endl;
            root["error"] = ERROR_CODE::VarifyExpired;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_res.body()) << jsonstr;
            return true;
        }
        if (varify_code != src_root["varifycode"].asString()) {
            std::cout << " varify code error" << std::endl;
            root["error"] = ERROR_CODE::VarifyCodeErr;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_res.body()) << jsonstr;
            return true;
        }
        //查找数据库判断用户是否存在
        int uid = MysqlMgr::GetInstance()->RegUser(name, email, pwd ,icon);
        if (uid == 0 || uid == -1) {
            std::cout << " user or email exist" << std::endl;
            root["error"] = ERROR_CODE::UserExist;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_res.body()) << jsonstr;
            return true;
        }
        root["error"] = 0;
        root["uid"] = uid;
        root["email"] = email;
        root["user"] = name;
        root["varifycode"] = src_root["varifycode"].asString();
        std::string jsonstr = root.toStyledString();
        beast::ostream(connection->_res.body()) << jsonstr;
        return true;
        });

    //修改密码逻辑
    RegPost("/reset_pwd", [](std::shared_ptr<HttpConnection> connection) {
        auto body_str = boost::beast::buffers_to_string(connection->_req.body().data());
        connection->_res.set(http::field::content_type, "text/json");
        Json::Value root;
        Json::Reader reader;
        Json::Value src_root;
        bool parse_success = reader.parse(body_str, src_root);
        if (!parse_success) {
            std::cout << "Failed to parse JSON data!" << std::endl;
            root["error"] = ERROR_CODE::JSON_ERROR;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_res.body()) << jsonstr;
            return true;
        }
        auto email = src_root["email"].asString();
        auto name = src_root["user"].asString();
        auto pwd = src_root["passwd"].asString();
        //先查找redis中email对应的验证码是否合理
        std::string  varify_code;
        bool b_get_varify = RedisMgr::GetInstance()->Get(CODE_HEAD + src_root["email"].asString(), varify_code);
        if (!b_get_varify) {
            std::cout << " get varify code expired" << std::endl;
            root["error"] = ERROR_CODE::VarifyExpired;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_res.body()) << jsonstr;
            return true;
        }
        if (varify_code != src_root["varifycode"].asString()) {
            std::cout << " varify code error" << std::endl;
            root["error"] = ERROR_CODE::VarifyCodeErr;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_res.body()) << jsonstr;
            return true;
        }
        //查询数据库判断用户名和邮箱是否匹配
        bool email_valid = MysqlMgr::GetInstance()->CheckEmail(name, email);
        if (!email_valid) {
            std::cout << " user email not match" << std::endl;
            root["error"] = ERROR_CODE::EmailNotMatch;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_res.body()) << jsonstr;
            return true;
        }
        //更新密码为最新密码
        bool b_up = MysqlMgr::GetInstance()->UpdatePwd(name, pwd);
        if (!b_up) {
            std::cout << " update pwd failed" << std::endl;
            root["error"] = ERROR_CODE::PasswdUpFailed;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_res.body()) << jsonstr;
            return true;
        }
        std::cout << "succeed to update password" << std::endl;
        root["error"] = 0;
        root["email"] = email;
        root["user"] = name;
        root["varifycode"] = src_root["varifycode"].asString();
        std::string jsonstr = root.toStyledString();
        beast::ostream(connection->_res.body()) << jsonstr;
        return true;
        });

    //用户登录逻辑
    RegPost("/user_login", [](std::shared_ptr<HttpConnection> connection) {
        auto body_str = boost::beast::buffers_to_string(connection->_req.body().data());
        connection->_res.set(http::field::content_type, "text/json");
        Json::Value root;
        Json::Reader reader;
        Json::Value src_root;
        bool parse_success = reader.parse(body_str, src_root);
        if (!parse_success) {
            std::cout << "Failed to parse JSON data!" << std::endl;
            root["error"] = ERROR_CODE::JSON_ERROR;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_res.body()) << jsonstr;
            return true;
        }
        auto email = src_root["email"].asString();
        auto pwd = src_root["passwd"].asString();
        UserInfo userInfo;
        //查询数据库判断用户名和密码是否匹配
        bool pwd_valid = MysqlMgr::GetInstance()->CheckPwd(email, pwd, userInfo);
        if (!pwd_valid) {
            std::cout << " user pwd not match" << std::endl;
            root["error"] = ERROR_CODE::PasswdInvalid;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_res.body()) << jsonstr;
            return true;
        }
        //查询StatusServer找到合适的连接
        auto reply = StatusGrpcClient::GetInstance()->GetChatServer(userInfo.uid);
        if (reply.error()) {
            std::cout << " grpc get chat server failed, error is " << reply.error() << std::endl;
            root["error"] = ERROR_CODE::RPC_ERROR;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_res.body()) << jsonstr;
            return true;
        }
        std::cout << "succeed to load userinfo uid is " << userInfo.uid << std::endl;

        root["error"] = 0;
        root["email"] = email;
		root["user"] = userInfo.name;
        root["uid"] = userInfo.uid;
        root["token"] = reply.token();
        root["host"] = reply.host();
        root["port"] = reply.port();
        std::string jsonstr = root.toStyledString();
        beast::ostream(connection->_res.body()) << jsonstr;
        return true;
        });
}

bool LogicSystem::HandleGet(std::string path, std::shared_ptr<HttpConnection> conn)
{
	if (_get_Handlers.find(path) == _get_Handlers.end()) {
		return false;
	}

	_get_Handlers[path](conn);
	return true;
}

void LogicSystem::RegGet(std::string url, HttpHandler handler)
{
	_get_Handlers.emplace(url,handler);
}

void LogicSystem::RegPost(std::string url, HttpHandler handler)
{
	_pos_Handlers.emplace(url, handler);
}

bool LogicSystem::HandlePost(std::string path, std::shared_ptr<HttpConnection> conn)
{
	if (_pos_Handlers.find(path) == _pos_Handlers.end()) {
		return false;
	}

	_pos_Handlers[path](conn);
	return true;
}

LogicSystem::~LogicSystem()
{
}
