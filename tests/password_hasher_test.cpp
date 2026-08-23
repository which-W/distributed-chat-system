#include "PasswordHasher.h"

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
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
