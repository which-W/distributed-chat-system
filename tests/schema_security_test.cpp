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
	if (schema.find("CREATE TABLE IF NOT EXISTS chat_message") == std::string::npos
		|| schema.find("UNIQUE KEY uk_chat_message_sender_client (sender_uid, client_message_id)")
			== std::string::npos
		|| schema.find("KEY idx_chat_message_pending (receiver_uid, acknowledged_at, id)")
			== std::string::npos
		|| schema.find("acknowledged_at TIMESTAMP NULL") == std::string::npos) {
		std::cerr << "fresh schema 未声明可靠聊天消息的幂等键和 ACK 索引\n";
		return 1;
	}
    return 0;
}
