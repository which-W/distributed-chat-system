#include "LogicSystem.h"
#include "VerifyGrpcClient.h"
#include "MysqlMgr.h"
#include "StatusGrpcClient.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <initializer_list>

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
    // 按 Unicode 码点计数并拒绝 Unicode 空白，与 Qt 客户端规则保持一致。
    if (password.empty() || password.size() > 128 * 4) return false;
    std::size_t code_points = 0;
    for (std::size_t offset = 0; offset < password.size();) {
        const auto lead = static_cast<unsigned char>(password[offset]);
        std::uint32_t value = 0;
        std::size_t width = 0;
        if (lead <= 0x7f) { value = lead; width = 1; }
        else if (lead >= 0xc2 && lead <= 0xdf) { value = lead & 0x1f; width = 2; }
        else if (lead >= 0xe0 && lead <= 0xef) { value = lead & 0x0f; width = 3; }
        else if (lead >= 0xf0 && lead <= 0xf4) { value = lead & 0x07; width = 4; }
        else return false;
        if (offset + width > password.size()) return false;
        for (std::size_t index = 1; index < width; ++index) {
            const auto byte = static_cast<unsigned char>(password[offset + index]);
            if ((byte & 0xc0) != 0x80) return false;
            value = (value << 6) | (byte & 0x3f);
        }
        if ((width == 2 && value < 0x80) || (width == 3 && value < 0x800)
            || (width == 4 && value < 0x10000) || value > 0x10ffff
            || (value >= 0xd800 && value <= 0xdfff)) return false;
        const bool whitespace = (value >= 0x09 && value <= 0x0d) || value == 0x20
            || value == 0x85 || value == 0xa0 || value == 0x1680
            || (value >= 0x2000 && value <= 0x200a) || value == 0x2028
            || value == 0x2029 || value == 0x202f || value == 0x205f || value == 0x3000;
        if (whitespace || ++code_points > 128) return false;
        offset += width;
    }
    return code_points >= 10;
}

bool hasStringFields(const Json::Value& object,
    std::initializer_list<const char*> field_names)
{
    if (!object.isObject()) return false;
    for (const char* field_name : field_names) {
        if (!object[field_name].isString()) return false;
    }
    return true;
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
		// 在访问字段前完成结构和类型验证，畸形 JSON 只能得到错误响应，不能抛出到网络线程。
		if (!prase_success || !hasStringFields(src_root, {"email"})) {
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
        if (!parse_success || !hasStringFields(
                src_root, {"email", "user", "passwd", "icon", "varifycode"})) {
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
        if (!parse_success || !hasStringFields(
                src_root, {"email", "user", "passwd", "varifycode"})) {
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
        if (!parse_success || !hasStringFields(src_root, {"email", "passwd"})) {
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
