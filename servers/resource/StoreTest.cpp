#include "EncryptedFileStore.h"
#include "ResourceLock.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <atomic>
#include <sodium.h>
#include <array>
#include <future>
#include <cstdlib>
#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

// 启动独立进程，验证进程退出后的密文恢复和操作系统锁释放，而非仅测试对象重建。
int child(const std::string& executable, const std::string& root, const std::string& mode) {
#ifdef _WIN32
    const char* arguments[] = {executable.c_str(),root.c_str(),mode.c_str(),nullptr};
    return static_cast<int>(::_spawnv(_P_WAIT,executable.c_str(),arguments));
#else
    const auto pid=::fork();
    if (pid==0) { ::execl(executable.c_str(),executable.c_str(),root.c_str(),mode.c_str(),static_cast<char*>(nullptr)); std::_Exit(127); }
    if (pid<0) return -1;
    int status=0;
    if (::waitpid(pid,&status,0)<0 || !WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
#endif
}

void check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void rejected(F call,const char* message) { bool threw=false; try { call(); } catch (...) { threw=true; } check(threw,message); }
int main(int argc,char** argv) {
    if (argc!=2 && argc!=3) return 2;
    auto root=std::filesystem::absolute(argv[1]);
    if (sodium_init()<0) return 2;
    if (argc==2) {
        std::array<unsigned char,8> random{}; randombytes_buf(random.data(),random.size());
        char hex[17]; sodium_bin2hex(hex,sizeof(hex),random.data(),random.size());
        root /= hex; // 每轮使用独立目录，之前失败遗留的密文不会影响本轮断言。
    }
    std::filesystem::create_directories(root);
    const std::string id="11111111-1111-4111-8111-111111111111", other="22222222-2222-4222-8222-222222222222";
    try {
        EncryptedFileStore first(root,std::string(64,'a')), second(root,std::string(64,'a'));
        const std::string processId="33333333-3333-4333-8333-333333333333";
        if (argc==3) {
            chat::resources::ResourceLock lock(root,processId);
            const std::string mode=argv[2];
            std::vector<unsigned char> bytes(chat::files::PlainChunkBytes,mode=="conflict" ? 18 : 17);
            if (mode=="crash") {
                first.create(processId); first.append(processId,0,bytes);
                // 不执行析构，模拟落盘后未确认就退出；锁由操作系统释放，密文必须保留。
                std::_Exit(73);
            }
            try { first.append(processId,0,bytes); }
            catch (...) { return mode=="conflict" ? 0 : 1; }
            return mode=="conflict" ? 1 : 0;
        }
        first.create(id); std::vector<unsigned char> a(chat::files::PlainChunkBytes,42), b(17,93);
        check(first.append(id,0,a)==a.size(),"initial durable append");
        check(second.append(id,0,a)==a.size(),"retry after lost DB confirmation");
        auto different=a; different[0]=43;
        rejected([&] { second.append(id,0,different); },"conflicting nonce must not be overwritten");
        check(second.append(id,a.size(),b)==a.size()+b.size(),"final partial chunk");
        check(first.read(id,0,a.size())==a,"first chunk preserved"); check(first.read(id,a.size(),b.size())==b,"partial chunk preserved");
        crypto_hash_sha256_state hash; crypto_hash_sha256_init(&hash); crypto_hash_sha256_update(&hash,a.data(),a.size()); crypto_hash_sha256_update(&hash,b.data(),b.size());
        unsigned char digest[32]; crypto_hash_sha256_final(&hash,digest); char hex[65]; sodium_bin2hex(hex,sizeof(hex),digest,sizeof(digest));
        check(first.sha256(id,a.size()+b.size())==hex,"full checksum");
        rejected([&] { first.create("../../escape"); },"path traversal");
        first.create(other); check(first.sha256(other,0)=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","empty file checksum");
        std::atomic<bool> acquired{false}, started{false}; std::thread waiter;
        bool serialized=false;
        {
            chat::resources::ResourceLock lock(root,id);
            waiter=std::thread([&] { started=true; chat::resources::ResourceLock next(root,id); acquired=true; });
            while (!started) std::this_thread::yield();
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); serialized=!acquired;
            chat::resources::ResourceLock independent(root,other);
        }
        waiter.join(); check(serialized,"same resource operations must serialize"); check(acquired,"lock release");
        first.remove(other); check(std::filesystem::exists(root/(other+".enc.lock")),"stable lock inode retained");
        // 截断未确认记录，模拟写入中途崩溃；残缺尾部不能作为可安全重试的完整记录。
        first.create(other); first.append(other,0,a);
        std::filesystem::resize_file(root/(other+".enc"),std::filesystem::file_size(root/(other+".enc"))-1);
        rejected([&] { second.append(other,0,a); },"torn record must reject reuse");
        // 翻转已有字节保证内容实际变化，验证认证标签能检测密文损坏。
        std::fstream corrupt(root/(id+".enc"),std::ios::binary|std::ios::in|std::ios::out);
        corrupt.seekg(30); char byte=0; corrupt.read(&byte,1); byte ^= 1;
        corrupt.seekp(30); corrupt.write(&byte,1); corrupt.close();
        rejected([&] { first.read(id,0,a.size()); },"ciphertext tamper detection");
        const auto executable=std::filesystem::absolute(argv[0]).string();
        check(child(executable,root.string(),"crash")==73,"child exits after durable append without confirmation");
        const auto durableSize=std::filesystem::file_size(root/(processId+".enc"));
        // 两个进程重放同一偏移：相同明文应幂等成功，不同明文不得复用 nonce 覆写。
        // 此处验证本机进程锁和存储恢复，不代替 MySQL 提交窗口或跨主机 NFS 验证。
        auto retryOne=std::async(std::launch::async,[&] { return child(executable,root.string(),"retry"); });
        auto retryTwo=std::async(std::launch::async,[&] { return child(executable,root.string(),"retry"); });
        check(retryOne.get()==0 && retryTwo.get()==0,"two processes replay the same durable chunk safely");
        check(child(executable,root.string(),"conflict")==0,"another process cannot replace confirmed ciphertext");
        check(std::filesystem::file_size(root/(processId+".enc"))==durableSize,"replayed chunks do not append duplicate records");
        check(first.read(processId,0,chat::files::PlainChunkBytes)==std::vector<unsigned char>(chat::files::PlainChunkBytes,17),"child ciphertext survives abrupt process exit and conflicting retries");
        first.remove(processId);
        first.remove(id); first.remove(other);
        std::cout<<"PASS: durable retries, conflict rejection, empty file, partial chunk, locks, torn writes, tamper, process crash and concurrent replay\n"; return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
