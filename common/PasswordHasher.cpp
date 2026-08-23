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

} // namespace chat::security
