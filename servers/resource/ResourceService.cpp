#include "ResourceService.h"
#include "GrpcTlsSupport.h"
#include "InternalRpcAuth.h"
#include <fstream>
#include <sstream>
#ifndef _WIN32
#include <sys/vfs.h>
#endif

namespace resource {
namespace {
std::filesystem::path storageRoot() {
    const std::filesystem::path root(setting("FileStorage", "Root"));
    require(!root.empty() && std::filesystem::is_directory(root), 503, "storage directory must already exist");
    if (setting("Resource", "RequireNfs", "true") == "true") {
#ifdef _WIN32
        throw Error(503, "NFS production deployment requires Linux");
#else
        struct statfs info{};
        require(::statfs(root.c_str(), &info) == 0 && info.f_type == 0x6969, 503, "NFS mount required");
        std::ifstream mounts("/proc/mounts"); std::string line; bool valid = false;
        while (std::getline(mounts, line)) {
            std::istringstream in(line); std::string source, path, type, options;
            in >> source >> path >> type >> options;
            if (std::filesystem::path(path) == root && (type == "nfs4" || type == "nfs") &&
                options.find("vers=4") != std::string::npos && options.find("local_lock=none") != std::string::npos &&
                options.find(",soft") == std::string::npos) valid = true;
        }
        require(valid, 503, "mount root requires NFSv4 hard,local_lock=none");
#endif
    }
    return root;
}
Response result(const Json::Value& value, unsigned status = 200) { Response r; r.status = status; r.body = json(value); return r; }
}
Service::Service()
    : root_(storageRoot()), store_(root_, setting("FileStorage", "MasterKey")),
      notification_workers_(4, 16) {
    // 节点 channel 由所有通知事件复用，避免每次 RPC 重新建立连接。
    std::istringstream peers(setting("PeerServer", "Servers"));
    std::string peer;
    auto& cfg = ConfigMgr::Inst();
    while (std::getline(peers, peer, ',')) {
        if (!peer.empty())
            peer_channels_.push_back(chat::grpc_tls::make_channel(
                cfg[peer]["Host"], cfg[peer]["Port"], chat::grpc_tls::from_config(cfg),
                cfg[peer]["TLSName"]));
    }
}

void Service::stopNotifications() { notification_workers_.stop(true); }

void Service::deliverNotification(NotificationEvent event) {
    // 未配置任何 Chat 节点时不能把事件误标为已送达。
    bool sent = !peer_channels_.empty();
    for (const auto& channel : peer_channels_) {
        auto stub = message::ChatService::NewStub(channel);
        grpc::ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));
        chat::internal_rpc::authenticate(context, setting("InternalRpc", "PeerToken"));
        if (event.kind == "file") {
            message::FileAvailableReq request;
            message::FileAvailableRsp reply;
            const auto& v = event.payload;
            request.set_id(v["id"].asString());
            request.set_fromuid(v["fromuid"].asInt());
            request.set_touid(v["touid"].asInt());
            request.set_name(v["name"].asString());
            request.set_mime(v["mime"].asString());
            request.set_total_size(v["total_size"].asUInt64());
            request.set_sha256(v["sha256"].asString());
            sent = stub->NotifyFileAvailable(&context, request, &reply).ok() &&
                   reply.error() == 0 && sent;
        } else {
            message::AvatarChangedReq request;
            message::AvatarChangedRsp reply;
            const auto& v = event.payload;
            request.set_uid(v["uid"].asInt());
            request.set_avatar_id(v["avatar_id"].asString());
            request.set_version(v["version"].asUInt64());
            sent = stub->NotifyAvatarChanged(&context, request, &reply).ok() && sent;
        }
    }
    // RPC 完成后才重新借用数据库连接；网络等待期间不持有行锁或连接。
    auto lease = database_.lease();
    execute(lease.get(), sent
        ? "UPDATE resource_outbox SET delivered_at=NOW() WHERE id=? AND delivered_at IS NULL"
        : "UPDATE resource_outbox SET attempts=attempts+1,next_attempt_at=DATE_ADD(NOW(),INTERVAL 10 SECOND) WHERE id=? AND delivered_at IS NULL",
        {event.id});
}
bool Service::ready() {
    try {
        storageRoot(); auto lease = database_.lease(); auto s = statement(lease.get(), "SELECT id FROM resource_outbox LIMIT 0");
        std::unique_ptr<sql::ResultSet> r(s->executeQuery()); redis_.command({"PING"});
        return std::filesystem::space(root_).available > 128ULL * 1024 * 1024;
    } catch (...) { return false; }
}
Json::Value Service::file(sql::Connection& c, const std::string& id) {
    require(chat::resources::validId(id), 400, "invalid resource id");
    auto s = statement(c, "SELECT *,expires_at<=NOW() AS expired FROM file_transfer WHERE id=?", {id});
    std::unique_ptr<sql::ResultSet> r(s->executeQuery()); require(r->next(), 404, "resource not found");
    Json::Value v; v["id"] = id; v["fromuid"] = r->getInt("sender_uid"); v["touid"] = r->getInt("receiver_uid");
    for (auto pair : {std::pair{"name", "original_name"}, {"mime", "mime_type"}, {"sha256", "sha256"}, {"status", "status"}})
        v[pair.first] = r->getString(pair.second).asStdString();
    v["total_size"] = Json::UInt64(r->getUInt64("total_size")); v["offset"] = Json::UInt64(r->getUInt64("uploaded_size"));
    v["expired"] = r->getBoolean("expired"); v["error"] = 0; return v;
}
Json::Value Service::create(int uid, const Json::Value& v) {
    require(v["touid"].isInt() && v["total_size"].isUInt64() && v["name"].isString() && v["sha256"].isString() && v["idempotency_key"].isString(), 400, "missing upload metadata");
    const auto name = v["name"].asString(), hash = v["sha256"].asString(), key = v["idempotency_key"].asString();
    const auto mime = v.get("mime", "application/octet-stream").asString(); auto size = v["total_size"].asUInt64();
    require(!name.empty() && name.size() <= 255 && name.find_first_of("/\\\r\n") == std::string::npos && name.find('\0') == std::string::npos && name != "." && name != "..", 400, "invalid filename");
    require(hash.size() == 64 && hash.find_first_not_of("0123456789abcdef") == std::string::npos && !key.empty() && key.size() <= 128 && mime.size() <= 128, 400, "invalid upload metadata");
    require(size <= chat::files::MaxFileBytes, 413, "file exceeds 100 MiB");
    auto l = database_.lease(); auto& c = l.get(); Transaction tx(c);
    auto lock = statement(c, "SELECT uid FROM user WHERE uid=? FOR UPDATE", {std::to_string(uid)});
    std::unique_ptr<sql::ResultSet> user(lock->executeQuery()); require(user->next(), 403, "unknown user"); user.reset();
    auto prior = statement(c, "SELECT id FROM file_transfer WHERE sender_uid=? AND idempotency_key=?", {std::to_string(uid), key});
    std::unique_ptr<sql::ResultSet> old(prior->executeQuery());
    if (old->next()) {
        auto f = file(c, old->getString(1).asStdString());
        require(f["touid"] == v["touid"] && f["name"].asString() == name && f["sha256"].asString() == hash && f["total_size"].asUInt64() == size && f["mime"].asString() == mime, 409, "idempotency conflict");
        require(!f["expired"].asBool() && f["status"] != "cancelled", 410, "upload expired"); tx.commit(); return f;
    }
    auto friends = statement(c, "SELECT self_id FROM friend WHERE self_id=? AND friend_id=?", {std::to_string(uid), std::to_string(v["touid"].asInt())});
    std::unique_ptr<sql::ResultSet> friendship(friends->executeQuery()); require(friendship->next(), 403, "recipient must be a friend");
    auto quota = statement(c, "SELECT COUNT(*) AS n,COALESCE(SUM(total_size),0) AS bytes FROM file_transfer WHERE sender_uid=? AND expires_at>NOW() AND status IN ('uploading','available','downloaded')", {std::to_string(uid)});
    std::unique_ptr<sql::ResultSet> usage(quota->executeQuery()); usage->next();
    require(usage->getUInt64("n") < 16 && usage->getUInt64("bytes") + size <= 1024ULL * 1024 * 1024, 429, "attachment quota exceeded");
    auto id = uuid(); chat::resources::ResourceLock guard(root_, id); store_.create(id);
    // Keep orphan ciphertext on ambiguous commit; maintenance removes only confirmed orphans.
    execute(c, "INSERT INTO file_transfer(id,sender_uid,receiver_uid,original_name,mime_type,total_size,sha256,idempotency_key,expires_at) VALUES(?,?,?,?,?,?,?,?,DATE_ADD(NOW(),INTERVAL 7 DAY))",
        {id, std::to_string(uid), std::to_string(v["touid"].asInt()), name, mime, std::to_string(size), hash, key});
    tx.commit(); return file(c, id);
}
Json::Value Service::chunk(int uid, const std::string& id, std::uint64_t offset, const std::string& bytes) {
    require(!bytes.empty() && bytes.size() <= 256 * 1024 && offset % chat::files::PlainChunkBytes == 0, 400, "invalid chunk");
    chat::resources::ResourceLock guard(root_, id); auto l = database_.lease(); auto& c = l.get(); auto f = file(c, id);
    require(f["fromuid"].asInt() == uid, 403, "upload owner required");
    require(!f["expired"].asBool() && f["status"] == "uploading", 410, "upload unavailable");
    const auto total = f["total_size"].asUInt64(); auto confirmed = f["offset"].asUInt64();
    require(offset <= total && bytes.size() <= total - offset && (bytes.size() % chat::files::PlainChunkBytes == 0 || offset + bytes.size() == total), 400, "invalid chunk length");
    if (offset > confirmed) { Error e(409, "offset conflict"); e.extra["offset"] = Json::UInt64(confirmed); throw e; }
    for (std::size_t at = 0; at < bytes.size(); at += chat::files::PlainChunkBytes) {
        auto count = std::min(chat::files::PlainChunkBytes, bytes.size() - at);
        std::vector<unsigned char> part(bytes.begin() + at, bytes.begin() + at + count);
        if (offset + at < confirmed) {
            auto previous = store_.read(id, offset + at, count); require(previous == part, 409, "immutable chunk conflict"); continue;
        }
        std::uint64_t next;
        try { next = store_.append(id, offset + at, part); }
        catch (const std::exception&) { throw Error(409, "conflicting or damaged ciphertext; start a new upload"); }
        require(execute(c, "UPDATE file_transfer SET uploaded_size=? WHERE id=? AND uploaded_size=? AND status='uploading' AND expires_at>NOW()",
            {std::to_string(next), id, std::to_string(confirmed)}) == 1, 503, "offset commit uncertain; query upload state");
        confirmed = next;
    }
    f["offset"] = Json::UInt64(confirmed); return f;
}
void Service::notify(sql::Connection& c, const std::string& key, const std::string& kind, const Json::Value& v) {
    execute(c, "INSERT INTO resource_outbox(event_key,kind,payload) VALUES(?,?,?) ON DUPLICATE KEY UPDATE event_key=event_key", {key, kind, json(v)});
}
Json::Value Service::complete(int uid, const std::string& id) {
    chat::resources::ResourceLock guard(root_, id); auto l = database_.lease(); auto& c = l.get(); auto f = file(c, id);
    require(f["fromuid"].asInt() == uid, 403, "upload owner required");
    require(!f["expired"].asBool() && f["status"] != "cancelled", 410, "resource expired");
    if (f["status"] == "available" || f["status"] == "downloaded") return f;
    require(f["offset"] == f["total_size"], 409, "upload incomplete");
    require(store_.sha256(id, f["total_size"].asUInt64()) == f["sha256"].asString(), 422, "checksum mismatch");
    Transaction tx(c);
    require(execute(c, "UPDATE file_transfer SET status='available',completed_at=NOW(),expires_at=DATE_ADD(NOW(),INTERVAL 7 DAY) WHERE id=? AND status='uploading'", {id}) == 1, 409, "state conflict");
    f["status"] = "available"; notify(c, "file:" + id, "file", f); tx.commit(); return f;
}
Response Service::handle(const Request& req) {
    auto target = std::string(req.target());
    if (target == "/health/live") { Json::Value v; v["alive"] = true; return result(v); }
    if (target == "/health/ready") { Json::Value v; v["ready"] = ready(); return result(v, v["ready"].asBool() ? 200 : 503); }
    const auto uid = redis_.authenticate(std::string(req[http::field::authorization]), req.body().size());
    const std::string prefix = "/api/resources/v1/";
    require(target.compare(0, prefix.size(), prefix) == 0, 404, "route not found"); target.erase(0, prefix.size());
    const auto method = req.method();
    if (method == http::verb::get && target.rfind("users/", 0) == 0 && target.size() > 13 && target.substr(target.size()-7) == "/avatar") {
        auto profileUid = number(target.substr(6,target.size()-13));
        auto lease = database_.lease(); auto s = statement(lease.get(), "SELECT avatar_id,avatar_version FROM user WHERE uid=?", {std::to_string(profileUid)});
        std::unique_ptr<sql::ResultSet> row(s->executeQuery()); require(row->next(),404,"user not found");
        Json::Value v; v["avatar_id"]=row->getString("avatar_id").asStdString(); v["version"]=Json::UInt64(row->getUInt64("avatar_version")); return result(v);
    }
    if (target == "users/me/avatar" || target.rfind("avatars/", 0) == 0) return avatar(uid, req);
    if (target == "uploads" && method == http::verb::post) return result(create(uid, parse(req.body())), 201);
    const auto slash = target.find('/'); require(slash != std::string::npos, 404, "route not found");
    const auto group = target.substr(0, slash); require(group == "uploads" || group == "files", 404, "route not found");
    const auto rest = target.substr(slash + 1); const auto end = rest.find('/');
    const auto id = rest.substr(0, end); const auto action = end == std::string::npos ? "" : rest.substr(end + 1);
    if (group == "uploads" && method == http::verb::put && action.rfind("chunks?offset=", 0) == 0)
        return result(chunk(uid, id, number(action.substr(14)), req.body()));
    if (group == "uploads" && action == "complete" && method == http::verb::post) return result(complete(uid, id));
    chat::resources::ResourceLock guard(root_, id); auto l = database_.lease(); auto& c = l.get(); auto f = file(c, id);
    require(f["fromuid"].asInt() == uid || f["touid"].asInt() == uid, 403, "resource access denied");
    if (group == "uploads") require(f["fromuid"].asInt() == uid, 403, "upload owner required");
    require(!f["expired"].asBool() && f["status"] != "cancelled", 410, "resource expired");
    if (method == http::verb::get && action.empty()) return result(f);
    if (method == http::verb::delete_ && action.empty()) {
        if (group == "uploads") require(f["status"] == "uploading", 409, "upload already published");
        execute(c, "UPDATE file_transfer SET status='cancelled',expires_at=NOW() WHERE id=?", {id});
        Response r; r.status = 204; return r;
    }
    require(group == "files" && f["touid"].asInt() == uid, 403, "receiver required");
    require(f["status"] == "available" || f["status"] == "downloaded", 409, "file not available");
    if (method == http::verb::post && action == "downloaded") {
        execute(c, "UPDATE file_transfer SET status='downloaded',downloaded_at=COALESCE(downloaded_at,NOW()),expires_at=LEAST(expires_at,DATE_ADD(NOW(),INTERVAL 1 DAY)) WHERE id=?", {id}); return result(f);
    }
    require(method == http::verb::get && action == "content", 404, "route not found");
    Response r; r.stream_id = id; r.content_type = "application/octet-stream"; r.length = f["total_size"].asUInt64();
    auto range = std::string(req[http::field::range]); const auto total = r.length;
    if (!range.empty()) {
        require(range.rfind("bytes=", 0) == 0 && range.find(',') == std::string::npos, 416, "single byte range required");
        auto dash = range.find('-', 6); require(dash != std::string::npos && total > 0, 416, "invalid range");
        auto first = range.substr(6, dash - 6), last = range.substr(dash + 1); std::uint64_t finish = total - 1;
        if (first.empty()) { auto suffix = number(last); require(suffix > 0, 416, "invalid suffix"); r.begin = total - std::min(total, suffix); }
        else { r.begin = number(first); if (!last.empty()) finish = std::min(finish, number(last)); }
        require(r.begin < total && finish >= r.begin, 416, "range outside file"); r.length = finish - r.begin + 1; r.status = 206;
        r.headers.emplace_back("Content-Range", "bytes " + std::to_string(r.begin) + "-" + std::to_string(finish) + "/" + std::to_string(total));
    }
    r.headers.emplace_back("Accept-Ranges", "bytes"); r.headers.emplace_back("ETag", "\"" + f["sha256"].asString() + "\"");
    r.headers.emplace_back("Cache-Control", "private, no-store"); return r;
}
std::vector<unsigned char> Service::read(const std::string& id, std::uint64_t offset) {
    chat::resources::ResourceLock guard(root_, id); auto l = database_.lease(); auto f = file(l.get(), id);
    require(!f["expired"].asBool() && (f["status"] == "available" || f["status"] == "downloaded"), 410, "file unavailable");
    return store_.read(id, offset, chat::files::PlainChunkBytes);
}
grpc::Status Service::ListPending(grpc::ServerContext* context, const message::ResourcePendingReq* req, message::ResourcePendingRsp* rsp) {
    const auto auth = chat::internal_rpc::authorize(*context, setting("InternalRpc", "PeerToken")); if (!auth.ok()) return auth;
    if (req->uid() <= 0) return {grpc::StatusCode::INVALID_ARGUMENT, "invalid uid"};
    try {
        auto l = database_.lease(); auto s = statement(l.get(), "SELECT id FROM file_transfer WHERE (sender_uid=? OR receiver_uid=?) AND status IN ('available','downloaded') AND expires_at>NOW() ORDER BY created_at LIMIT 32", {std::to_string(req->uid()), std::to_string(req->uid())});
        std::unique_ptr<sql::ResultSet> rows(s->executeQuery());
        while (rows->next()) rsp->add_metadata_json(json(file(l.get(), rows->getString(1).asStdString())));
        return grpc::Status::OK;
    } catch (...) { return {grpc::StatusCode::UNAVAILABLE, "resource database unavailable"}; }
}
void Service::maintenance() {
    std::vector<NotificationEvent> claimed;
    {
        auto claim_lease = database_.lease();
        auto& c = claim_lease.get();
        Transaction tx(c);
        // 短事务一次领取最多 16 条；60 秒领取租约让进程崩溃后可重新投递。
        auto s = statement(c, "SELECT id,kind,payload FROM resource_outbox WHERE delivered_at IS NULL AND next_attempt_at<=NOW() ORDER BY id LIMIT 16 FOR UPDATE SKIP LOCKED");
        std::unique_ptr<sql::ResultSet> rows(s->executeQuery());
        while (rows->next()) {
            claimed.push_back({rows->getString("id").asStdString(),
                               rows->getString("kind").asStdString(),
                               parse(rows->getString("payload").asStdString())});
        }
        rows.reset();
        for (const auto& event : claimed)
            execute(c, "UPDATE resource_outbox SET next_attempt_at=DATE_ADD(NOW(),INTERVAL 60 SECOND) WHERE id=? AND delivered_at IS NULL", {event.id});
        tx.commit();
    }
    for (auto& event : claimed) {
        const auto key = std::hash<std::string>{}(event.id);
        if (!notification_workers_.post(key, [this, event]() { deliverNotification(event); })) {
            // 工作池满时把领取租约缩短到正常重试间隔。
            auto retry_lease = database_.lease();
            execute(retry_lease.get(), "UPDATE resource_outbox SET next_attempt_at=DATE_ADD(NOW(),INTERVAL 10 SECOND) WHERE id=? AND delivered_at IS NULL", {event.id});
        }
    }
    auto l = database_.lease(); auto& c = l.get();
    auto avatars = statement(c,"SELECT id FROM resource_avatar WHERE expires_at<=NOW() LIMIT 16");
    std::unique_ptr<sql::ResultSet> avatarRows(avatars->executeQuery()); std::vector<std::string> expiredAvatars;
    while (avatarRows->next()) expiredAvatars.push_back(avatarRows->getString(1).asStdString());
    avatarRows.reset();
    for (const auto& id:expiredAvatars) {
        chat::resources::ResourceLock lock(root_,id); Transaction tx(c);
        auto query=statement(c,"SELECT variants FROM resource_avatar WHERE id=? AND expires_at<=NOW() AND NOT EXISTS (SELECT 1 FROM user WHERE avatar_id=?) FOR UPDATE",{id,id});
        std::unique_ptr<sql::ResultSet> row(query->executeQuery());
        if (row->next()) {
            auto variants=parse(row->getString(1).asStdString());
            for (const auto& variant:variants) store_.remove(variant["id"].asString());
            execute(c,"DELETE FROM resource_avatar WHERE id=?",{id});
        }
        tx.commit();
    }
    auto s = statement(c, "SELECT id FROM file_transfer WHERE expires_at<=NOW() OR status='cancelled' LIMIT 32");
    std::unique_ptr<sql::ResultSet> rows(s->executeQuery()); std::vector<std::string> ids;
    while (rows->next()) ids.push_back(rows->getString(1).asStdString()); rows.reset();
    for (const auto& id : ids) {
        chat::resources::ResourceLock lock(root_, id); Transaction tx(c);
        auto check = statement(c, "SELECT id FROM file_transfer WHERE id=? AND (expires_at<=NOW() OR status='cancelled') FOR UPDATE", {id});
        std::unique_ptr<sql::ResultSet> row(check->executeQuery());
        if (row->next()) { store_.remove(id); execute(c, "DELETE FROM resource_outbox WHERE event_key=?", {"file:"+id}); execute(c, "DELETE FROM file_transfer WHERE id=?", {id}); } tx.commit();
    }
}
}
namespace resource {
grpc::Status Service::GetMetadata(grpc::ServerContext* context, const message::ResourceMetadataReq* req, message::ResourceMetadataRsp* rsp) {
    auto auth = chat::internal_rpc::authorize(*context, setting("InternalRpc","PeerToken")); if (!auth.ok()) return auth;
    try {
        auto l=database_.lease(); auto v=file(l.get(),req->id());
        if (v["expired"].asBool() || (v["fromuid"].asInt()!=req->uid() && v["touid"].asInt()!=req->uid()))
            return {grpc::StatusCode::PERMISSION_DENIED,"resource unavailable"};
        rsp->set_metadata_json(json(v)); return grpc::Status::OK;
    } catch (...) { return {grpc::StatusCode::UNAVAILABLE,"resource unavailable"}; }
}
}
