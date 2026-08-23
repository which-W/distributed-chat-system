#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace chat::routing {

struct ServerLoad {
    std::string name;
    int connection_count;
};

std::optional<std::string> selectLeastLoaded(const std::vector<ServerLoad>& healthy_servers,
                                             std::size_t round_robin_cursor);

} // namespace chat::routing
