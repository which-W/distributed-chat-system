#include "LogicSystem.h"
#include "ChatLogger.h"
#include "MysqlMgr.h"
#include "PasswordHasher.h"
#include "RedisMgr.h"
#include "StatusGrpcClient.h"
#include "VerifyGrpcClient.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <array>
#include <sodium.h>

namespace {
constexpr int kResumeSeconds = 15 * 60;

std::string hexDigest(const std::string& value) {
    std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
    crypto_hash_sha256(digest.data(), reinterpret_cast<const unsigned char*>(value.data()),
                       value.size());
    std::string encoded(digest.size() * 2 + 1, '\0');
    sodium_bin2hex(encoded.data(), encoded.size(), digest.data(), digest.size());
    encoded.resize(digest.size() * 2);
    return encoded;
}

std::string newResumeToken() {
    std::array<unsigned char, 32> bytes{};
    randombytes_buf(bytes.data(), bytes.size());
    std::string token(bytes.size() * 2 + 1, '\0');
    sodium_bin2hex(token.data(), token.size(), bytes.data(), bytes.size());
    token.resize(bytes.size() * 2);
    return token;
}

bool validResumeToken(const std::string& token) {
    return token.size() == 64 &&
           std::all_of(token.begin(), token.end(), [](unsigned char ch) {
               return std::isxdigit(ch) != 0;
           });
}

void sessionError(const std::shared_ptr<HttpConnection>& connection, Json::Value& response,
                  http::status status) {
    response = Json::Value(Json::objectValue);
    response["error"] = status == http::status::unauthorized ? ERROR_CODE_UNAUTHORIZED
                                                            : ERROR_CODE_SERVICE_UNAVAILABLE;
    connection->SetJsonError(status, response["error"].asInt());
}

// 只在数据库中的密码哈希指纹仍匹配时接受续期凭证。
bool readResumeSession(const std::shared_ptr<HttpConnection>& connection,
                       const std::string& token, Json::Value& response, int& uid,
                       std::string& record) {
    if (!validResumeToken(token)) {
        sessionError(connection, response, http::status::unauthorized);
        return false;
    }
    const auto key = "resume_session_" + hexDigest(token);
    const auto read = RedisMgr::GetInstance()->ReadSession(key, record);
    if (read != RedisMgr::SessionReadResult::Found) {
        sessionError(connection, response, read == RedisMgr::SessionReadResult::Missing
                                              ? http::status::unauthorized
                                              : http::status::service_unavailable);
        return false;
    }
    const auto delimiter = record.find(':');
    try {
        if (delimiter == std::string::npos || delimiter == 0)
            throw std::invalid_argument("invalid resume record");
        std::size_t parsed = 0;
        uid = std::stoi(record.substr(0, delimiter), &parsed);
        if (uid <= 0 || parsed != delimiter)
            throw std::invalid_argument("invalid resume uid");
    } catch (const std::exception&) {
        sessionError(connection, response, http::status::unauthorized);
        return false;
    }
    std::string current;
    bool unavailable = false;
    if (!MysqlMgr::GetInstance()->PasswordFingerprint(uid, current, &unavailable)) {
        sessionError(connection, response, unavailable ? http::status::service_unavailable
                                                       : http::status::unauthorized);
        return false;
    }
    const auto stored = record.substr(delimiter + 1);
    if (stored.size() != current.size() || sodium_memcmp(stored.data(), current.data(),
                                                         stored.size()) != 0) {
        sessionError(connection, response, http::status::unauthorized);
        return false;
    }
    return true;
}

void addChatEndpoint(Json::Value& root, int uid, const GetChatServerRsp& reply) {
    root["uid"] = uid;
    root["token"] = reply.token();
    const char* resource_url = std::getenv("CHAT_RESOURCE_BASE_URL");
    root["resource_base_url"] = resource_url ? resource_url : "https://localhost/api/resources/v1";
    root["resource_protocol_version"] = 1;
    root["host"] = reply.host();
    root["port"] = reply.port();
    root["transport"] = reply.transport();
    root["tls_server_name"] = reply.tls_server_name();
}

std::string normalizeEmail(std::string email) {
    email.erase(email.begin(), std::find_if(email.begin(), email.end(),
                                            [](unsigned char ch) { return !std::isspace(ch); }));
    email.erase(std::find_if(email.rbegin(), email.rend(),
                             [](unsigned char ch) { return !std::isspace(ch); })
                    .base(),
                email.end());
    std::transform(email.begin(), email.end(), email.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return email;
}

bool consumeVerificationCode(const std::string& email, const std::string& submitted_code,
                             Json::Value& response) {
    const auto result = RedisMgr::GetInstance()->ConsumeVerificationCode(email, submitted_code);
    if (result == RedisMgr::VerificationResult::Success) {
        return true;
    }
    response["error"] = result == RedisMgr::VerificationResult::Expired ? ERROR_CODE::VarifyExpired
                                                                        : ERROR_CODE::VarifyCodeErr;
    return false;
}

bool validNewPassword(const std::string& password) {
    // 按 Unicode 码点计数并拒绝 Unicode 空白，与 Qt 客户端规则保持一致。
    if (password.empty() || password.size() > 128 * 4)
        return false;
    std::size_t code_points = 0;
    for (std::size_t offset = 0; offset < password.size();) {
        const auto lead = static_cast<unsigned char>(password[offset]);
        std::uint32_t value = 0;
        std::size_t width = 0;
        if (lead <= 0x7f) {
            value = lead;
            width = 1;
        } else if (lead >= 0xc2 && lead <= 0xdf) {
            value = lead & 0x1f;
            width = 2;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            value = lead & 0x0f;
            width = 3;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            value = lead & 0x07;
            width = 4;
        } else
            return false;
        if (offset + width > password.size())
            return false;
        for (std::size_t index = 1; index < width; ++index) {
            const auto byte = static_cast<unsigned char>(password[offset + index]);
            if ((byte & 0xc0) != 0x80)
                return false;
            value = (value << 6) | (byte & 0x3f);
        }
        if ((width == 2 && value < 0x80) || (width == 3 && value < 0x800) ||
            (width == 4 && value < 0x10000) || value > 0x10ffff ||
            (value >= 0xd800 && value <= 0xdfff))
            return false;
        const bool whitespace =
            (value >= 0x09 && value <= 0x0d) || value == 0x20 || value == 0x85 || value == 0xa0 ||
            value == 0x1680 || (value >= 0x2000 && value <= 0x200a) || value == 0x2028 ||
            value == 0x2029 || value == 0x202f || value == 0x205f || value == 0x3000;
        if (whitespace || ++code_points > 128)
            return false;
        offset += width;
    }
    return code_points >= 10;
}

bool hasStringFields(const Json::Value& object, std::initializer_list<const char*> field_names) {
    if (!object.isObject())
        return false;
    for (const char* field_name : field_names) {
        if (!object[field_name].isString())
            return false;
    }
    return true;
}

} // namespace

LogicSystem::LogicSystem() {
    // 测试
    RegGet("/get_test", [](std::shared_ptr<HttpConnection> conn) {
        beast::ostream(conn->_res.body()) << "Hello, World!";
    });

    // 验证码逻辑
    RegPost("/post_email", [](std::shared_ptr<HttpConnection> conn) {
        std::string body_str = boost::beast::buffers_to_string(conn->_req.body().data());
        conn->_res.set(http::field::content_type, "test/json");
        Json::Value root;
        Json::Reader reader;
        Json::Value src_root;
        bool prase_success = reader.parse(body_str, src_root);
        // 在访问字段前完成结构和类型验证，畸形 JSON 只能得到错误响应，不能抛出到网络线程。
        if (!prase_success || !hasStringFields(src_root, {"email"})) {
            chat::observability::stream(chat::observability::Level::Info)
                << "JSON parse error" << std::endl;
            root["error"] = ERROR_CODE::JSON_ERROR;
            std::string jsonstr = root.toStyledString();
            beast::ostream(conn->_res.body()) << jsonstr;
            return true;
        }
        auto email = normalizeEmail(src_root["email"].asString());
        GetVarifyRsp _rsp =
            VerifyGrpcClient::GetInstance()->GetVarifyCode(email, conn->_request_id);
        root["error"] = _rsp.error();
        root["email"] = email;
        std::string jsonstr = root.toStyledString();
        beast::ostream(conn->_res.body()) << jsonstr;
        return true;
    });

    // 注册逻辑
    RegPost("/user_register", [](std::shared_ptr<HttpConnection> connection) {
        auto body_str = boost::beast::buffers_to_string(connection->_req.body().data());
        connection->_res.set(http::field::content_type, "text/json");
        Json::Value root;
        Json::Reader reader;
        Json::Value src_root;
        bool parse_success = reader.parse(body_str, src_root);
        if (!parse_success ||
            !hasStringFields(src_root, {"email", "user", "passwd", "icon", "varifycode"})) {
            chat::observability::stream(chat::observability::Level::Info)
                << "Failed to parse JSON data!" << std::endl;
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
        // 查找数据库判断用户是否存在
        int uid = MysqlMgr::GetInstance()->RegUser(name, email, pwd, icon);
        if (uid == 0 || uid == -1) {
            chat::observability::stream(chat::observability::Level::Info)
                << " user or email exist" << std::endl;
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

    // 修改密码逻辑
    RegPost("/reset_pwd", [](std::shared_ptr<HttpConnection> connection) {
        auto body_str = boost::beast::buffers_to_string(connection->_req.body().data());
        connection->_res.set(http::field::content_type, "text/json");
        Json::Value root;
        Json::Reader reader;
        Json::Value src_root;
        bool parse_success = reader.parse(body_str, src_root);
        if (!parse_success ||
            !hasStringFields(src_root, {"email", "user", "passwd", "varifycode"})) {
            chat::observability::stream(chat::observability::Level::Info)
                << "Failed to parse JSON data!" << std::endl;
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
        // 先检查账户与数据库状态，避免因数据库故障白白消费验证码。
        bool database_unavailable = false;
        bool email_valid = MysqlMgr::GetInstance()->CheckEmail(name, email, &database_unavailable);
        if (!email_valid) {
            connection->_res.result(database_unavailable ? http::status::service_unavailable
                                                         : http::status::bad_request);
            root["error"] = database_unavailable ? ERROR_CODE::RPC_ERROR
                                                   : ERROR_CODE::EmailNotMatch;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_res.body()) << jsonstr;
            return true;
        }
        const auto request_token = src_root["reset_request_token"].isString()
                                       ? src_root["reset_request_token"].asString()
                                       : std::string{};
        std::string password_hash;
        std::string grant_key;
        if (request_token.empty()) {
            // 旧客户端仍可提交原字段，但数据库更新失败后需重新获取验证码。
            if (!consumeVerificationCode(email, src_root["varifycode"].asString(), root)) {
                beast::ostream(connection->_res.body()) << root.toStyledString();
                return true;
            }
        } else {
            if (!validResumeToken(request_token)) {
                connection->_res.result(http::status::bad_request);
                root["error"] = ERROR_CODE_BAD_REQUEST;
                beast::ostream(connection->_res.body()) << root.toStyledString();
                return true;
            }
            grant_key = "reset_grant_" + hexDigest(request_token);
            auto redis = RedisMgr::GetInstance();
            std::string grant;
            auto grant_read = redis->ReadSession(grant_key, grant);
            if (grant_read == RedisMgr::SessionReadResult::RedisError) {
                sessionError(connection, root, http::status::service_unavailable);
                return true;
            }
            if (grant_read == RedisMgr::SessionReadResult::Missing) {
                Json::Value record;
                record["email"] = email;
                record["user"] = name;
                record["hash"] = chat::security::PasswordHasher::hash(pwd);
                const auto issue = redis->ConsumeResetCodeAndGrant(
                    email, src_root["varifycode"].asString(), grant_key, record.toStyledString());
                if (issue == RedisMgr::ResetGrantResult::Created) {
                    grant = record.toStyledString();
                } else if (issue == RedisMgr::ResetGrantResult::Exists) {
                    grant_read = redis->ReadSession(grant_key, grant);
                    if (grant_read != RedisMgr::SessionReadResult::Found) {
                        sessionError(connection, root, http::status::service_unavailable);
                        return true;
                    }
                } else {
                    connection->_res.result(issue == RedisMgr::ResetGrantResult::RedisError
                                                ? http::status::service_unavailable
                                                : http::status::bad_request);
                    root["error"] = issue == RedisMgr::ResetGrantResult::RedisError
                                        ? ERROR_CODE::RPC_ERROR
                                        : issue == RedisMgr::ResetGrantResult::Expired
                                              ? ERROR_CODE::VarifyExpired
                                              : ERROR_CODE::VarifyCodeErr;
                    beast::ostream(connection->_res.body()) << root.toStyledString();
                    return true;
                }
            }
            Json::Value record;
            Json::Reader grant_reader;
            if (!grant_reader.parse(grant, record) || !record.isObject() ||
                !record["email"].isString() || record["email"].asString() != email ||
                !record["user"].isString() || record["user"].asString() != name ||
                !record["hash"].isString() ||
                !chat::security::PasswordHasher::verify(pwd, record["hash"].asString())) {
                connection->_res.result(http::status::unauthorized);
                root["error"] = ERROR_CODE_UNAUTHORIZED;
                beast::ostream(connection->_res.body()) << root.toStyledString();
                return true;
            }
            password_hash = record["hash"].asString();
        }
        // 同一随机请求的 Argon2 哈希固定，数据库故障时可安全重试。
        bool b_up = request_token.empty()
                        ? MysqlMgr::GetInstance()->UpdatePwd(name, pwd)
                        : MysqlMgr::GetInstance()->UpdatePwdHash(name, email, password_hash);
        if (!b_up) {
            connection->_res.result(http::status::service_unavailable);
            chat::observability::stream(chat::observability::Level::Info)
                << " update pwd failed" << std::endl;
            root["error"] = ERROR_CODE::PasswdUpFailed;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_res.body()) << jsonstr;
            return true;
        }
        chat::observability::stream(chat::observability::Level::Info)
            << "succeed to update password" << std::endl;
        if (!grant_key.empty())
            RedisMgr::GetInstance()->Del(grant_key);
        root["error"] = 0;
        root["email"] = email;
        root["user"] = name;
        std::string jsonstr = root.toStyledString();
        beast::ostream(connection->_res.body()) << jsonstr;
        return true;
    });

    // 用户登录逻辑
    RegPost("/user_login", [](std::shared_ptr<HttpConnection> connection) {
        auto body_str = boost::beast::buffers_to_string(connection->_req.body().data());
        connection->_res.set(http::field::content_type, "text/json");
        Json::Value root;
        Json::Reader reader;
        Json::Value src_root;
        bool parse_success = reader.parse(body_str, src_root);
        if (!parse_success || !hasStringFields(src_root, {"email", "passwd"})) {
            chat::observability::stream(chat::observability::Level::Info)
                << "Failed to parse JSON data!" << std::endl;
            root["error"] = ERROR_CODE::JSON_ERROR;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_res.body()) << jsonstr;
            return true;
        }
        auto email = normalizeEmail(src_root["email"].asString());
        auto pwd = src_root["passwd"].asString();
        UserInfo userInfo;
        // 查询数据库判断用户名和密码是否匹配
        bool unavailable = false;
        bool pwd_valid = MysqlMgr::GetInstance()->CheckPwd(email, pwd, userInfo, &unavailable);
        if (!pwd_valid) {
            chat::observability::stream(chat::observability::Level::Info)
                << " user pwd not match" << std::endl;
            root["error"] = unavailable ? ERROR_CODE::RPC_ERROR : ERROR_CODE::PasswdInvalid;
            root["retryable"] = unavailable;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_res.body()) << jsonstr;
            return true;
        }
        // 查询StatusServer找到合适的连接
        auto reply =
            StatusGrpcClient::GetInstance()->GetChatServer(userInfo.uid, connection->_request_id);
        if (reply.error()) {
            chat::observability::stream(chat::observability::Level::Info)
                << " grpc get chat server failed, error is " << reply.error() << std::endl;
            root["error"] = ERROR_CODE::RPC_ERROR;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_res.body()) << jsonstr;
            return true;
        }
        chat::observability::stream(chat::observability::Level::Info)
            << "succeed to load userinfo uid is " << userInfo.uid << std::endl;

        root["error"] = 0;
        root["email"] = email;
        root["user"] = userInfo.name;
        addChatEndpoint(root, userInfo.uid, reply);
        // Redis 仅保存随机凭证的摘要；密码重置会使记录中的指纹立即失配。
        const auto resume_token = newResumeToken();
        if (!RedisMgr::GetInstance()->SetWithTtl("resume_session_" + hexDigest(resume_token),
                                                std::to_string(userInfo.uid) + ":" +
                                                    userInfo.password_fingerprint,
                                                kResumeSeconds)) {
            sessionError(connection, root, http::status::service_unavailable);
            return true;
        }
        root["resume_token"] = resume_token;
        root["resume_expires_in"] = kResumeSeconds;
        std::string jsonstr = root.toStyledString();
        beast::ostream(connection->_res.body()) << jsonstr;
        return true;
    });

    RegPost("/session_renew", [](std::shared_ptr<HttpConnection> connection) {
        connection->_res.set(http::field::content_type, "application/json");
        Json::Value request, response;
        Json::Reader reader;
        if (!reader.parse(boost::beast::buffers_to_string(connection->_req.body().data()), request) ||
            !hasStringFields(request, {"resume_token"})) {
            connection->_res.result(http::status::bad_request);
            response["error"] = ERROR_CODE_BAD_REQUEST;
            beast::ostream(connection->_res.body()) << response.toStyledString();
            return true;
        }
        const auto token = request["resume_token"].asString();
        int uid = 0;
        std::string record;
        if (!readResumeSession(connection, token, response, uid, record))
            return true;
        // 比较记录并续期是单个 Redis 脚本，防止与退出撤销竞争时复活凭证。
        const auto result = RedisMgr::GetInstance()->RenewSession(
            "resume_session_" + hexDigest(token), record, kResumeSeconds);
        if (result != RedisMgr::SessionReadResult::Found) {
            sessionError(connection, response, result == RedisMgr::SessionReadResult::Missing
                                                  ? http::status::unauthorized
                                                  : http::status::service_unavailable);
            return true;
        }
        response["error"] = 0;
        response["resume_expires_in"] = kResumeSeconds;
        beast::ostream(connection->_res.body()) << response.toStyledString();
        return true;
    });

    RegPost("/session_refresh", [](std::shared_ptr<HttpConnection> connection) {
        connection->_res.set(http::field::content_type, "application/json");
        Json::Value request, response;
        Json::Reader reader;
        if (!reader.parse(boost::beast::buffers_to_string(connection->_req.body().data()), request) ||
            !hasStringFields(request, {"resume_token"})) {
            connection->_res.result(http::status::bad_request);
            response["error"] = ERROR_CODE_BAD_REQUEST;
            beast::ostream(connection->_res.body()) << response.toStyledString();
            return true;
        }
        const auto token = request["resume_token"].asString();
        int uid = 0;
        std::string record;
        if (!readResumeSession(connection, token, response, uid, record))
            return true;
        auto reply = StatusGrpcClient::GetInstance()->GetChatServer(uid, connection->_request_id);
        if (reply.error()) {
            sessionError(connection, response, http::status::service_unavailable);
            return true;
        }
        // Status RPC 期间可能发生退出或密码重置；回包前再次确认凭证仍有效。
        int current_uid = 0;
        std::string current_record;
        if (!readResumeSession(connection, token, response, current_uid, current_record))
            return true;
        if (current_uid != uid || current_record != record) {
            sessionError(connection, response, http::status::unauthorized);
            return true;
        }
        response["error"] = 0;
        addChatEndpoint(response, uid, reply);
        beast::ostream(connection->_res.body()) << response.toStyledString();
        return true;
    });

    RegPost("/session_logout", [](std::shared_ptr<HttpConnection> connection) {
        connection->_res.set(http::field::content_type, "application/json");
        Json::Value request, response;
        Json::Reader reader;
        if (!reader.parse(boost::beast::buffers_to_string(connection->_req.body().data()), request) ||
            !hasStringFields(request, {"resume_token"}) ||
            !validResumeToken(request["resume_token"].asString())) {
            connection->_res.result(http::status::bad_request);
            response["error"] = ERROR_CODE_BAD_REQUEST;
        } else if (!RedisMgr::GetInstance()->Del(
                       "resume_session_" + hexDigest(request["resume_token"].asString()))) {
            connection->_res.result(http::status::service_unavailable);
            response["error"] = ERROR_CODE_SERVICE_UNAVAILABLE;
        } else {
            response["error"] = 0;
        }
        beast::ostream(connection->_res.body()) << response.toStyledString();
        return true;
    });
}

bool LogicSystem::HandleGet(std::string path, std::shared_ptr<HttpConnection> conn) {
    if (_get_Handlers.find(path) == _get_Handlers.end()) {
        return false;
    }

    _get_Handlers[path](conn);
    return true;
}

void LogicSystem::RegGet(std::string url, HttpHandler handler) {
    _get_Handlers.emplace(url, handler);
}

void LogicSystem::RegPost(std::string url, HttpHandler handler) {
    _pos_Handlers.emplace(url, handler);
}

bool LogicSystem::HandlePost(std::string path, std::shared_ptr<HttpConnection> conn) {
    if (_pos_Handlers.find(path) == _pos_Handlers.end()) {
        return false;
    }

    _pos_Handlers[path](conn);
    return true;
}

LogicSystem::~LogicSystem() {}
