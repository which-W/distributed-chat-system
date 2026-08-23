#include "ChatServerSelector.h"

#include <algorithm>
#include <limits>

namespace chat::routing {

std::optional<std::string> selectLeastLoaded(const std::vector<ServerLoad>& healthy_servers,
                                             std::size_t round_robin_cursor) {
    if (healthy_servers.empty()) {
        return std::nullopt;
    }

    int minimum = std::numeric_limits<int>::max();
    for (const auto& server : healthy_servers) {
        minimum = std::min(minimum, server.connection_count);
    }

    std::vector<std::string> candidates;
    for (const auto& server : healthy_servers) {
        if (server.connection_count == minimum) {
            candidates.push_back(server.name);
        }
    }
    std::sort(candidates.begin(), candidates.end());
    return candidates[round_robin_cursor % candidates.size()];
}

} // namespace chat::routing
