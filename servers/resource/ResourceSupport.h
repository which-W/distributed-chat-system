#pragma once
#include "ConfigMgr.h"
#include "MysqlConnectionPool.h"
#include "RedisConnectionPool.h"
#include "ResourceToken.h"
#include <json/json.h>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <charconv>
#include <optional>

namespace resource {
struct Error : std::runtime_error {
    unsigned status; Json::Value extra;
    Error(unsigned code, const std::string& message) : std::runtime_error(message), status(code) {}
};
inline void require(bool ok, unsigned status, const char* message) { if (!ok) throw Error(status, message); }
inline std::string uuid() { return boost::uuids::to_string(boost::uuids::random_generator()()); }
inline std::string json(const Json::Value& v) { Json::StreamWriterBuilder b; b["indentation"] = ""; return Json::writeString(b, v); }
inline Json::Value parse(const std::string& s) {
    Json::CharReaderBuilder b; b["rejectDupKeys"] = true; b["failIfExtra"] = true;
    std::unique_ptr<Json::CharReader> r(b.newCharReader()); Json::Value v; std::string e;
    require(r->parse(s.data(), s.data() + s.size(), &v, &e) && v.isObject(), 400, "invalid JSON object"); return v;
}
inline std::uint64_t number(const std::string& s) {
    std::uint64_t n = 0; auto r = std::from_chars(s.data(), s.data() + s.size(), n);
    require(!s.empty() && r.ec == std::errc{} && r.ptr == s.data() + s.size(), 400, "invalid number"); return n;
}
inline std::string setting(const char* section, const char* key, const std::string& fallback = {}) {
    auto v = ConfigMgr::Inst()[section][key]; return v.empty() ? fallback : v;
}
class Database {
    chat::storage::MySqlPool pool_;
public:
    Database() : pool_("tcp://" + setting("Mysql", "Host", "127.0.0.1") + ":" + setting("Mysql", "Port", "3306"),
        setting("Mysql", "User"), setting("Mysql", "Passwd"), setting("Mysql", "Schema", "wgt"), 8) {}
    class Lease {
        chat::storage::MySqlPool& pool_; std::unique_ptr<chat::storage::SqlConnection> c_;
    public:
        explicit Lease(chat::storage::MySqlPool& p) : pool_(p), c_(p.getConnection()) { if (!c_) throw Error(503, "database unavailable"); }
        ~Lease() { pool_.returnConnection(std::move(c_)); }
        sql::Connection& get() { return *c_->_con; }
    };
    Lease lease() { return Lease(pool_); }
};
using Args = std::vector<std::string>;
inline std::unique_ptr<sql::PreparedStatement> statement(sql::Connection& c, const std::string& q, const Args& a = {}) {
    std::unique_ptr<sql::PreparedStatement> p(c.prepareStatement(q));
    for (std::size_t i = 0; i < a.size(); ++i) p->setString(static_cast<unsigned>(i + 1), a[i]); return p;
}
inline int execute(sql::Connection& c, const std::string& q, const Args& a = {}) { return statement(c, q, a)->executeUpdate(); }
class Transaction {
    sql::Connection& c_; bool committed_ = false;
public:
    explicit Transaction(sql::Connection& c) : c_(c) { c_.setAutoCommit(false); }
    void commit() { c_.commit(); committed_ = true; c_.setAutoCommit(true); }
    ~Transaction() { if (!committed_) { try { c_.rollback(); c_.setAutoCommit(true); } catch (...) {} } }
};
class Redis {
    chat::storage::RedisConnectionPool pool_;
public:
    Redis() : pool_(8, setting("Redis", "Host", "127.0.0.1"), std::stoi(setting("Redis", "Port", "6379")),
        setting("Redis", "User"), setting("Redis", "Passwd")) {}
    using Reply = std::unique_ptr<redisReply, decltype(&freeReplyObject)>;
    Reply command(const Args& a) {
        auto* c = pool_.getConnection(); if (!c) throw Error(503, "redis unavailable");
        std::vector<const char*> argv; std::vector<std::size_t> lens;
        for (auto& v : a) { argv.push_back(v.data()); lens.push_back(v.size()); }
        Reply r(static_cast<redisReply*>(redisCommandArgv(c, static_cast<int>(a.size()), argv.data(), lens.data())), freeReplyObject);
        pool_.returnConnection(c); if (!r || r->type == REDIS_REPLY_ERROR) throw Error(503, "redis unavailable"); return r;
    }
    std::optional<std::string> get(const std::string& k) {
        auto r = command({"GET", k}); if (r->type == REDIS_REPLY_NIL) return {};
        if (r->type != REDIS_REPLY_STRING) throw Error(503, "invalid redis reply"); return std::string(r->str, r->len);
    }
    int authenticate(const std::string& auth, std::size_t bytes) {
        require(auth.size() == 71 && auth.substr(0, 7) == "Bearer ", 401, "resource credential required");
        auto token = get(chat::resources::tokenKey(auth.substr(7))); require(token.has_value(), 401, "resource credential expired");
        auto v = parse(*token); auto session = get(chat::resources::sessionKey(v["session"].asString()));
        require(session && *session == v["generation"].asString(), 401, "resource credential revoked");
        const int uid = v["uid"].asInt(); require(uid > 0, 401, "invalid identity");
        static const std::string script = "local t=redis.call('TIME'); local k=KEYS[1]..':'..t[1]; "
            "local n=redis.call('HINCRBY',k,'requests',1); local b=redis.call('HINCRBY',k,'bytes',ARGV[1]); "
            "redis.call('EXPIRE',k,2); if n>128 or b>8388608 then return 0 end; return 1";
        auto rate = command({"EVAL", script, "1", "resource_rate:" + std::to_string(uid), std::to_string(bytes)});
        require(rate->type == REDIS_REPLY_INTEGER && rate->integer == 1, 429, "resource rate limit"); return uid;
    }
};
}
