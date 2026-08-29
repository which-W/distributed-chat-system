#pragma once

#include <string>

namespace chat::security {

struct PasswordVerificationResult {
    bool valid = false;
    bool upgrade_required = false;
    std::string upgraded_hash;
};

class PasswordHasher {
  public:
    static std::string hash(const std::string& password);
    static bool verify(const std::string& password, const std::string& encoded_hash);
    static bool isEncodedHash(const std::string& value);
    static bool needsRehash(const std::string& encoded_hash);
    // 仅用于旧客户端预哈希账户的一次性登录迁移，禁止用于新注册或重置。
    static std::string legacyClientSha256(const std::string& password);
    // scheme 只能取数据库声明的两种值；未知值必须拒绝，不能猜测为 raw。
    static PasswordVerificationResult verifyCredential(const std::string& raw_password,
                                                       const std::string& stored_hash,
                                                       const std::string& password_scheme);
};

} // namespace chat::security
