#pragma once

#include <string>

namespace chat::security {

class PasswordHasher {
  public:
    static std::string hash(const std::string& password);
    static bool verify(const std::string& password, const std::string& encoded_hash);
    static bool isEncodedHash(const std::string& value);
    static bool needsRehash(const std::string& encoded_hash);
};

} // namespace chat::security
