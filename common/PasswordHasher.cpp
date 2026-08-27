#include "PasswordHasher.h"

#include <sodium.h>

#include <array>
#include <mutex>
#include <stdexcept>

namespace chat::security {
namespace {

void ensureSodiumInitialized() {
    static std::once_flag init_flag;
    static int init_result = -1;
    std::call_once(init_flag, [] { init_result = sodium_init(); });
    if (init_result < 0) {
        throw std::runtime_error("libsodium initialization failed");
    }
}

} // namespace

std::string PasswordHasher::hash(const std::string& password) {
    ensureSodiumInitialized();
    if (password.empty()) {
        throw std::invalid_argument("password must not be empty");
    }

    std::array<char, crypto_pwhash_STRBYTES> encoded{};
    if (crypto_pwhash_str_alg(encoded.data(), password.data(),
                              static_cast<unsigned long long>(password.size()),
                              crypto_pwhash_OPSLIMIT_MODERATE, crypto_pwhash_MEMLIMIT_MODERATE,
                              crypto_pwhash_ALG_ARGON2ID13) != 0) {
        throw std::runtime_error("password hashing failed");
    }
    return encoded.data();
}

bool PasswordHasher::verify(const std::string& password, const std::string& encoded_hash) {
    ensureSodiumInitialized();
    if (!isEncodedHash(encoded_hash)) {
        return false;
    }
    return crypto_pwhash_str_verify(encoded_hash.c_str(), password.data(),
                                    static_cast<unsigned long long>(password.size())) == 0;
}

bool PasswordHasher::isEncodedHash(const std::string& value) {
    return value.rfind("$argon2id$", 0) == 0;
}

bool PasswordHasher::needsRehash(const std::string& encoded_hash) {
    ensureSodiumInitialized();
    if (!isEncodedHash(encoded_hash)) {
        return true;
    }
    return crypto_pwhash_str_needs_rehash(encoded_hash.c_str(), crypto_pwhash_OPSLIMIT_MODERATE,
                                          crypto_pwhash_MEMLIMIT_MODERATE) != 0;
}

std::string PasswordHasher::legacyClientSha256(const std::string& password) {
	ensureSodiumInitialized();
	std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
	crypto_hash_sha256(digest.data(),
		reinterpret_cast<const unsigned char*>(password.data()), password.size());
	std::array<char, crypto_hash_sha256_BYTES * 2 + 1> encoded{};
	sodium_bin2hex(encoded.data(), encoded.size(), digest.data(), digest.size());
	return encoded.data();
}

PasswordVerificationResult PasswordHasher::verifyCredential(
    const std::string& raw_password, const std::string& stored_hash,
    const std::string& password_scheme)
{
    PasswordVerificationResult result;
    const bool raw_scheme = password_scheme == "argon2id_raw";
    const bool legacy_scheme = password_scheme == "legacy_client_sha256";
    if (!raw_scheme && !legacy_scheme) {
        return result;
    }

    const std::string candidate = legacy_scheme
        ? legacyClientSha256(raw_password) : raw_password;
    const bool legacy_plaintext = !isEncodedHash(stored_hash);
    if (legacy_plaintext) {
        result.valid = candidate.size() == stored_hash.size()
            && sodium_memcmp(candidate.data(), stored_hash.data(), candidate.size()) == 0;
    }
    else {
        result.valid = verify(candidate, stored_hash);
    }
    if (!result.valid) {
        return result;
    }

    result.upgrade_required = legacy_plaintext || legacy_scheme || needsRehash(stored_hash);
    if (result.upgrade_required) {
        // 升级结果始终基于原始口令，完成后不再走客户端 SHA-256 兼容分支。
        result.upgraded_hash = hash(raw_password);
    }
    return result;
}

} // namespace chat::security
