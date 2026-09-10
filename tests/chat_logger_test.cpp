#include "ChatLogger.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <json/json.h>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
void environment(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}
} // namespace

int main() {
    const auto directory = std::filesystem::temp_directory_path() / "chat-observability-test";
    std::filesystem::remove_all(directory);
    environment("CHAT_LOG_DIR", directory.string());
    environment("CHAT_LOG_CONSOLE", "false");
    environment("CHAT_LOG_FILE_ENABLED", "true");
    environment("CHAT_LOG_QUEUE_CAPACITY", "8192");
    chat::observability::initialize("test_service");

    std::vector<std::thread> producers;
    for (int producer = 0; producer < 4; ++producer) {
        producers.emplace_back([producer] {
            for (int item = 0; item < 100; ++item) {
                chat::observability::log(
                    chat::observability::Level::Info, "test.concurrent", "record",
                    {{"uid", std::int64_t(producer)}, {"msg_id", std::to_string(item)}});
            }
        });
    }
    for (auto& producer : producers)
        producer.join();
    chat::observability::stream(chat::observability::Level::Warn)
        << "lookup chat_ticket_super-secret-value failed";
    chat::observability::flush();
    const auto dropped = chat::observability::droppedCount();
    chat::observability::shutdown();

    std::ifstream input(directory / "test_service.jsonl");
    std::string line;
    std::size_t records = 0;
    while (std::getline(input, line)) {
        Json::Value value;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream stream(line);
        if (!Json::parseFromStream(builder, stream, &value, &errors)) {
            std::cerr << errors << '\n';
            return 1;
        }
        for (const char* field :
             {"timestamp", "level", "service", "pid", "thread", "event", "message"}) {
            if (!value.isMember(field))
                return 1;
        }
        if (line.find("super-secret-value") != std::string::npos)
            return 1;
        ++records;
    }
    input.close();
    std::filesystem::remove_all(directory);
    if (records != 401 || dropped != 0)
        return 1;
    const auto request_id = chat::observability::newRequestId();
    return request_id.size() == 36 ? 0 : 1;
}
