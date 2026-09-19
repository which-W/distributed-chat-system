#pragma once
#include <sodium.h>
#include <stdexcept>
#include <string>

namespace chat::resources {
inline constexpr int TokenSeconds = 900;
inline std::string tokenDigest(const std::string& token) {
    unsigned char digest[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(digest, reinterpret_cast<const unsigned char*>(token.data()), token.size());
    char hex[sizeof(digest) * 2 + 1];
    sodium_bin2hex(hex, sizeof(hex), digest, sizeof(digest));
    return hex;
}
inline std::string newToken() {
    if (sodium_init() < 0) throw std::runtime_error("sodium initialization failed");
    unsigned char bytes[32];
    randombytes_buf(bytes, sizeof(bytes));
    char hex[65];
    sodium_bin2hex(hex, sizeof(hex), bytes, sizeof(bytes));
    return hex;
}
inline std::string tokenKey(const std::string& token) { return "resource_token:" + tokenDigest(token); }
inline std::string sessionKey(const std::string& session) { return "resource_session:" + session; }
}
