#include "LogicSystem.h"
#include "VerifyGrpcClient.h"
#include "MysqlMgr.h"
#include "StatusGrpcClient.h"
#include <algorithm>
#include <cctype>

namespace {

std::string normalizeEmail(std::string email)
{
    email.erase(email.begin(), std::find_if(email.begin(), email.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    email.erase(std::find_if(email.rbegin(), email.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), email.end());
    std::transform(email.begin(), email.end(), email.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return email;
}

bool consumeVerificationCode(
    const std::string& email, const std::string& submitted_code, Json::Value& response)
{
    const auto result = RedisMgr::GetInstance()->ConsumeVerificationCode(email, submitted_code);
    if (result == RedisMgr::VerificationResult::Success) {
        return true;
    }
    response["error"] = result == RedisMgr::VerificationResult::Expired
        ? ERROR_CODE::VarifyExpired
        : ERROR_CODE::VarifyCodeErr;
    return false;
}

bool validNewPassword(const std::string& password)
{
    return password.size() >= 10 && password.size() <= 128;
}

} // namespace

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

            auto email = normalizeEmail(src_root["email"].asString());
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
        auto email = normalizeEmail(src_root["email"].asString());
        auto name = src_root["user"].asString();
        auto pwd = src_root["passwd"].asString();
        auto icon = src_root["icon"].asString();

        if (!validNewPassword(pwd)) {
            root["error"] = ERROR_CODE::PasswdInvalid;
            beast::ostream(connection->_res.body()) << root.toStyledString();
            return true;
        }

        if (!consumeVerificationCode(email, src_root["varifycode"].asString(), root)) {
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
        auto email = normalizeEmail(src_root["email"].asString());
        auto name = src_root["user"].asString();
        auto pwd = src_root["passwd"].asString();
        if (!validNewPassword(pwd)) {
            root["error"] = ERROR_CODE::PasswdInvalid;
            beast::ostream(connection->_res.body()) << root.toStyledString();
            return true;
        }
        if (!consumeVerificationCode(email, src_root["varifycode"].asString(), root)) {
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
        auto email = normalizeEmail(src_root["email"].asString());
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
        root["transport"] = reply.transport();
        root["tls_server_name"] = reply.tls_server_name();
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
