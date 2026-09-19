#include "ResourceService.h"
#include <png.h>
#include <turbojpeg.h>
#include <array>

namespace resource {
namespace {
struct Image { int width = 0, height = 0; std::vector<unsigned char> rgb; };
Image decode(const std::string& bytes) {
    Image out;
    if (bytes.size() >= 8 && png_sig_cmp(reinterpret_cast<png_const_bytep>(bytes.data()), 0, 8) == 0) {
        // Reject APNG by scanning PNG chunk headers before decoding.
        for (std::size_t pos = 8; pos + 12 <= bytes.size();) {
            const auto* b = reinterpret_cast<const unsigned char*>(bytes.data() + pos);
            const std::uint64_t length = (std::uint64_t(b[0]) << 24) | (std::uint64_t(b[1]) << 16) | (std::uint64_t(b[2]) << 8) | b[3];
            require(length <= bytes.size() - pos - 12, 422, "invalid PNG chunk");
            require(bytes.compare(pos + 4, 4, "acTL") != 0, 415, "animated PNG unsupported"); pos += static_cast<std::size_t>(length) + 12;
        }
        png_image image{}; image.version = PNG_IMAGE_VERSION;
        require(png_image_begin_read_from_memory(&image, bytes.data(), bytes.size()) != 0, 422, "invalid PNG");
        if (!image.width || !image.height || std::uint64_t(image.width) * image.height > 16000000) { png_image_free(&image); throw Error(413, "avatar pixel limit"); }
        out.width = static_cast<int>(image.width); out.height = static_cast<int>(image.height); image.format = PNG_FORMAT_RGB;
        out.rgb.resize(PNG_IMAGE_SIZE(image)); png_color background{255,255,255};
        const auto ok = png_image_finish_read(&image, &background, out.rgb.data(), 0, nullptr); png_image_free(&image); require(ok != 0, 422, "invalid PNG pixels");
    } else if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 255 && static_cast<unsigned char>(bytes[1]) == 216) {
        auto decoder = tjInitDecompress(); require(decoder != nullptr, 503, "JPEG decoder unavailable");
        int subsamp = 0, colors = 0;
        auto input = reinterpret_cast<const unsigned char*>(bytes.data());
        if (tjDecompressHeader3(decoder, input, static_cast<unsigned long>(bytes.size()), &out.width, &out.height, &subsamp, &colors) != 0 ||
            out.width <= 0 || out.height <= 0 || std::uint64_t(out.width) * out.height > 16000000) {
            tjDestroy(decoder); throw Error(422, "invalid JPEG or pixel limit exceeded");
        }
        out.rgb.resize(std::size_t(out.width) * out.height * 3);
        const auto ok = tjDecompress2(decoder, input, static_cast<unsigned long>(bytes.size()), out.rgb.data(), out.width, 0, out.height, TJPF_RGB, TJFLAG_STOPONWARNING);
        tjDestroy(decoder); require(ok == 0, 422, "invalid JPEG pixels");
    } else throw Error(415, "JPEG or PNG required");
    return out;
}
std::vector<unsigned char> thumbnail(const Image& image, unsigned size) {
    std::vector<unsigned char> pixels(size * size * 3);
    const int side = std::min(image.width, image.height), left = (image.width - side) / 2, top = (image.height - side) / 2;
    // Area sampling: deterministic crop, bounded by the decoded image pixel budget.
    for (unsigned y = 0; y < size; ++y) for (unsigned x = 0; x < size; ++x) {
        int x0 = left + int(x * side / size), x1 = left + int((x + 1) * side / size);
        int y0 = top + int(y * side / size), y1 = top + int((y + 1) * side / size);
        x1 = std::max(x1, x0 + 1); y1 = std::max(y1, y0 + 1);
        std::array<std::uint64_t,3> sum{};
        for (int yy = y0; yy < y1; ++yy) for (int xx = x0; xx < x1; ++xx)
            for (unsigned k = 0; k < 3; ++k) sum[k] += image.rgb[(std::size_t(yy) * image.width + xx) * 3 + k];
        for (unsigned k = 0; k < 3; ++k) pixels[(y * size + x) * 3 + k] = static_cast<unsigned char>(sum[k] / ((x1-x0)*(y1-y0)));
    }
    png_image png{}; png.version = PNG_IMAGE_VERSION; png.width = size; png.height = size; png.format = PNG_FORMAT_RGB;
    png_alloc_size_t count = 0; require(png_image_write_to_memory(&png, nullptr, &count, 0, pixels.data(), 0, nullptr) != 0, 503, "PNG encoding failed");
    std::vector<unsigned char> output(count);
    require(png_image_write_to_memory(&png, output.data(), &count, 0, pixels.data(), 0, nullptr) != 0, 503, "PNG encoding failed"); output.resize(count); return output;
}
}
Response Service::avatar(int uid, const Request& req) {
    const std::string path(req.target()); const std::string prefix = "/api/resources/v1/avatars/";
    auto lease = database_.lease(); auto& c = lease.get();
    if (path == "/api/resources/v1/users/me/avatar" && req.method() == http::verb::put) {
        require(!req.body().empty() && req.body().size() <= 5 * 1024 * 1024, 413, "avatar size limit");
        auto version = std::string(req[http::field::if_match]); require(!version.empty(), 428, "If-Match avatar version required");
        if (version.size() >= 2 && version.front() == '"' && version.back() == '"') version = version.substr(1, version.size()-2);
        const auto expected = number(version); auto image = decode(req.body()); auto id = uuid(); Json::Value variants;
        // Reserve ownership before files are written, allowing interrupted processing to be reclaimed.
        execute(c, "INSERT INTO resource_avatar(id,owner_uid,variants,expires_at) VALUES(?,?,?,DATE_ADD(NOW(),INTERVAL 1 DAY))", {id, std::to_string(uid), "{}"});
        chat::resources::ResourceLock avatarLock(root_, id);
        for (unsigned size : {64U, 128U, 256U}) {
            auto output = thumbnail(image, size); auto variant = uuid();
            variants[std::to_string(size)]["id"] = variant; variants[std::to_string(size)]["bytes"] = Json::UInt64(output.size());
            execute(c, "UPDATE resource_avatar SET variants=? WHERE id=?", {json(variants), id});
            store_.create(variant);
            for (std::size_t at = 0; at < output.size(); at += chat::files::PlainChunkBytes) {
                auto end = std::min(output.size(), at + chat::files::PlainChunkBytes);
                store_.append(variant, at, std::vector<unsigned char>(output.begin()+at, output.begin()+end));
            }
        }
        Transaction tx(c); auto s = statement(c, "SELECT avatar_id,avatar_version FROM user WHERE uid=? FOR UPDATE", {std::to_string(uid)});
        std::unique_ptr<sql::ResultSet> row(s->executeQuery()); require(row->next(), 403, "unknown user");
        require(row->getUInt64("avatar_version") == expected, 412, "avatar changed; refresh profile");
        auto old = row->getString("avatar_id").asStdString(); row.reset();
        execute(c, "UPDATE user SET avatar_id=?,avatar_version=avatar_version+1 WHERE uid=?", {id, std::to_string(uid)});
        execute(c, "UPDATE resource_avatar SET expires_at=NULL WHERE id=?", {id});
        if (!old.empty()) execute(c, "UPDATE resource_avatar SET expires_at=DATE_ADD(NOW(),INTERVAL 1 DAY) WHERE id=?", {old});
        Json::Value v; v["uid"] = uid; v["avatar_id"] = id; v["version"] = Json::UInt64(expected + 1); v["error"] = 0;
        notify(c, "avatar:" + id, "avatar", v); tx.commit(); Response r; r.body = json(v); return r;
    }
    require(req.method() == http::verb::get && path.rfind(prefix, 0) == 0, 404, "route not found");
    auto tail = path.substr(prefix.size()); auto slash = tail.find('/'); require(slash != std::string::npos, 404, "avatar size required");
    auto id = tail.substr(0, slash), size = tail.substr(slash+1);
    require(chat::resources::validId(id) && (size == "64" || size == "128" || size == "256"), 400, "invalid avatar");
    chat::resources::ResourceLock lock(root_, id);
    auto s = statement(c, "SELECT variants FROM resource_avatar WHERE id=? AND (expires_at IS NULL OR expires_at>NOW())", {id});
    std::unique_ptr<sql::ResultSet> row(s->executeQuery()); require(row->next(), 404, "avatar not found");
    auto variants = parse(row->getString(1).asStdString()); require(variants.isMember(size), 404, "avatar unavailable");
    Response r; r.content_type = "image/png"; const auto etag = "\"" + id + ":" + size + "\"";
    r.headers = {{"Cache-Control", "private, max-age=86400"}, {"ETag", etag}, {"Vary", "Authorization"}};
    if (std::string(req[http::field::if_none_match]) == etag) { r.status = 304; return r; }
    auto variant = variants[size]; auto total = variant["bytes"].asUInt64(); require(total <= 1024 * 1024, 503, "invalid avatar metadata");
    for (std::uint64_t at = 0; at < total; at += chat::files::PlainChunkBytes) {
        auto bytes = store_.read(variant["id"].asString(), at, chat::files::PlainChunkBytes);
        r.body.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    return r;
}
}
