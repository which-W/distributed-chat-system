#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>

namespace chat::config {

inline std::filesystem::path resolve_file(const std::string& fallback_name = "config.ini") {
    if (const char* value = std::getenv("CHAT_CONFIG_FILE"); value != nullptr && *value != '\0') {
        return std::filesystem::path(value);
    }
    return std::filesystem::current_path() / fallback_name;
}

inline const char* env(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? value : nullptr;
}

}  // namespace chat::config
