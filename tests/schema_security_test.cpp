#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main()
{
    std::ifstream input(CHAT_SCHEMA_FILE, std::ios::binary);
    const std::string schema((std::istreambuf_iterator<char>(input)), {});
    if (schema.find("password_scheme VARCHAR(32) NOT NULL DEFAULT 'argon2id_raw'")
            == std::string::npos
        || schema.find("pwd, password_scheme, nick") == std::string::npos) {
        std::cerr << "fresh schema 未声明原始口令 Argon2id scheme\n";
        return 1;
    }
    return 0;
}
