#include "PasswordHasher.h"
#include "PasswordUpgradeGuard.h"

#include <sodium.h>

#include <array>
#include <exception>
#include <iostream>

int main() {
    try {
        const std::string password = "correct horse battery staple";
        const auto encoded = chat::security::PasswordHasher::hash(password);
        if (!chat::security::PasswordHasher::isEncodedHash(encoded)) {
            std::cerr << "expected an Argon2id encoded hash\n";
            return 1;
        }
        if (!chat::security::PasswordHasher::verify(password, encoded)) {
            std::cerr << "correct password was rejected\n";
            return 1;
        }
        if (chat::security::PasswordHasher::verify("wrong password", encoded)) {
            std::cerr << "wrong password was accepted\n";
            return 1;
        }
        if (chat::security::PasswordHasher::verify(password, password)) {
            std::cerr << "plaintext value was accepted as a hash\n";
            return 1;
        }
        // 旧客户端迁移必须与 Qt 的 SHA-256 十六进制表示完全一致。
        if (chat::security::PasswordHasher::legacyClientSha256("password") !=
            "5e884898da28047151d0e56f8dc6292773603d0d6aabbdd62a11ef721d1542d8") {
            std::cerr << "legacy client SHA-256 compatibility changed\n";
            return 1;
        }
        const auto raw_result =
            chat::security::PasswordHasher::verifyCredential(password, encoded, "argon2id_raw");
        if (!raw_result.valid || raw_result.upgrade_required) {
            std::cerr << "current raw Argon2id credential was not preserved\n";
            return 1;
        }

        const auto legacy_sha = chat::security::PasswordHasher::legacyClientSha256(password);
        const auto legacy_plain = chat::security::PasswordHasher::verifyCredential(
            password, legacy_sha, "legacy_client_sha256");
        if (!legacy_plain.valid || !legacy_plain.upgrade_required ||
            !chat::security::PasswordHasher::verify(password, legacy_plain.upgraded_hash)) {
            std::cerr << "legacy plaintext SHA credential did not migrate to raw Argon2id\n";
            return 1;
        }

        const auto legacy_argon = chat::security::PasswordHasher::hash(legacy_sha);
        const auto legacy_encoded = chat::security::PasswordHasher::verifyCredential(
            password, legacy_argon, "legacy_client_sha256");
        if (!legacy_encoded.valid || !legacy_encoded.upgrade_required ||
            !chat::security::PasswordHasher::verify(password, legacy_encoded.upgraded_hash)) {
            std::cerr << "legacy Argon2id(SHA256) credential did not migrate\n";
            return 1;
        }
        {
            std::string current_hash = legacy_argon;
            std::string current_scheme = "legacy_client_sha256";
            const bool upgraded = chat::security::ApplyPasswordUpgradeIfCurrent(
                legacy_encoded, legacy_argon, current_scheme,
                [&current_hash, &current_scheme](const std::string& replacement,
                                                 const std::string& expected_hash,
                                                 const std::string& expected_scheme) {
                    if (current_hash != expected_hash || current_scheme != expected_scheme)
                        return 0;
                    current_hash = replacement;
                    current_scheme = "argon2id_raw";
                    return 1;
                });
            if (!upgraded || current_scheme != "argon2id_raw" ||
                !chat::security::PasswordHasher::verify(password, current_hash)) {
                std::cerr << "credential CAS upgrade did not apply\n";
                return 1;
            }
        }
        {
            // 模拟 SELECT 后发生密码重置：旧登录迁移必须 CAS 失败且保留新密码。
            const auto reset_hash = chat::security::PasswordHasher::hash("reset password");
            std::string current_hash = reset_hash;
            std::string current_scheme = "argon2id_raw";
            const bool upgraded = chat::security::ApplyPasswordUpgradeIfCurrent(
                legacy_encoded, legacy_argon, "legacy_client_sha256",
                [&current_hash, &current_scheme](const std::string& replacement,
                                                 const std::string& expected_hash,
                                                 const std::string& expected_scheme) {
                    if (current_hash != expected_hash || current_scheme != expected_scheme)
                        return 0;
                    current_hash = replacement;
                    current_scheme = "argon2id_raw";
                    return 1;
                });
            if (upgraded || current_hash != reset_hash || current_scheme != "argon2id_raw") {
                std::cerr << "stale login overwrote a concurrent password reset\n";
                return 1;
            }
        }
        if (chat::security::PasswordHasher::verifyCredential("wrong", legacy_argon,
                                                             "legacy_client_sha256")
                .valid ||
            chat::security::PasswordHasher::verifyCredential(password, encoded, "unknown_scheme")
                .valid) {
            std::cerr << "wrong password or unknown scheme was accepted\n";
            return 1;
        }
        std::array<char, crypto_pwhash_STRBYTES> weak_hash{};
        if (crypto_pwhash_str_alg(weak_hash.data(), password.data(), password.size(),
                                  crypto_pwhash_OPSLIMIT_INTERACTIVE,
                                  crypto_pwhash_MEMLIMIT_INTERACTIVE,
                                  crypto_pwhash_ALG_ARGON2ID13) != 0) {
            std::cerr << "failed to create weak Argon2id fixture\n";
            return 1;
        }
        const auto rehash = chat::security::PasswordHasher::verifyCredential(
            password, weak_hash.data(), "argon2id_raw");
        if (!rehash.valid || !rehash.upgrade_required ||
            !chat::security::PasswordHasher::verify(password, rehash.upgraded_hash)) {
            std::cerr << "weak raw Argon2id credential was not rehashed\n";
            return 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
