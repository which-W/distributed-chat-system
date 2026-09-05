#pragma once

#include <string>

namespace chat::messages {

struct TextMessage {
    std::string client_message_id;
    int sender_uid = 0;
    int receiver_uid = 0;
    std::string content;
};

} // namespace chat::messages
