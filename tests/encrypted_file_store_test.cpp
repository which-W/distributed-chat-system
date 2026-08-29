#include "EncryptedFileStore.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main()
{
    const auto root = std::filesystem::temp_directory_path() / "chat-encrypted-store-test";
    std::filesystem::remove_all(root);
    try {
        const std::string id = "01234567-89ab-cdef-0123-456789abcdef";
        EncryptedFileStore store(root, std::string(64, '1'));
        store.create(id);
        std::vector<unsigned char> first(chat::files::PlainChunkBytes, 'A');
        std::vector<unsigned char> second {'B', 'C', 'D'};
        if (store.append(id, 0, first) != first.size()
            || store.append(id, first.size(), second) != first.size() + second.size()
            || store.read(id, 0, first.size()) != first
            || store.read(id, first.size(), second.size()) != second
            || store.sha256(id, first.size() + second.size()).size() != 64) {
            std::cerr << "加密分片往返或摘要验证失败\n"; return 1;
        }
        // 密文文件中不应出现长段明文，防止实现退化为仅改扩展名的伪加密。
        std::ifstream encrypted(root / (id + ".enc"), std::ios::binary);
        const std::string raw((std::istreambuf_iterator<char>(encrypted)), {});
        encrypted.close();
        if (raw.find(std::string(64, 'A')) != std::string::npos) {
            std::cerr << "密文文件泄露了明文内容\n"; return 1;
        }
        bool rejected = false;
        try { store.create("../escape"); } catch (const std::invalid_argument&) { rejected = true; }
        if (!rejected || std::filesystem::exists(root.parent_path() / "escape.enc")) {
            std::cerr << "路径穿越文件 ID 未被拒绝\n"; return 1;
        }
        EncryptedFileStore wrongKey(root, std::string(64, '2'));
        rejected = false;
        try { wrongKey.read(id, 0, first.size()); } catch (const std::runtime_error&) { rejected = true; }
        if (!rejected) { std::cerr << "错误密钥未触发认证失败\n"; return 1; }
        store.remove(id);
        std::filesystem::remove_all(root);
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(root);
        std::cerr << error.what() << '\n'; return 1;
    }
}
