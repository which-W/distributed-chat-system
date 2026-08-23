#include "ChatServerSelector.h"

#include <iostream>
#include <vector>

int main() {
    using chat::routing::selectLeastLoaded;
    using chat::routing::ServerLoad;

    if (selectLeastLoaded({}, 0).has_value()) {
        std::cerr << "empty healthy set selected a server\n";
        return 1;
    }

    const std::vector<ServerLoad> unequal{{"chat-b", 7}, {"chat-a", 2}};
    if (selectLeastLoaded(unequal, 0) != "chat-a") {
        std::cerr << "least-loaded server was not selected\n";
        return 1;
    }

    const std::vector<ServerLoad> tied{{"chat-b", 3}, {"chat-a", 3}};
    if (selectLeastLoaded(tied, 0) != "chat-a" || selectLeastLoaded(tied, 1) != "chat-b" ||
        selectLeastLoaded(tied, 2) != "chat-a") {
        std::cerr << "round-robin tie breaking is not deterministic\n";
        return 1;
    }
    return 0;
}
