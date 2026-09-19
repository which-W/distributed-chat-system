#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace chat::resources {
class ResourceStore {
public:
    virtual ~ResourceStore() = default;
    virtual void create(const std::string& id) = 0;
    virtual std::uint64_t append(const std::string& id,std::uint64_t offset,const std::vector<unsigned char>& plaintext) = 0;
    virtual std::vector<unsigned char> read(const std::string& id,std::uint64_t offset,std::size_t maximum_bytes) const = 0;
    virtual std::string sha256(const std::string& id,std::uint64_t total_size) const = 0;
    virtual void remove(const std::string& id) = 0;
};
}
