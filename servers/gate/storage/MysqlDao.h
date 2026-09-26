#pragma once
#include "ConfigMgr.h"
#include "MysqlConnectionPool.h"
#include "const.h"
#include <memory>
#include <string>

using SqlConnection = chat::storage::SqlConnection;
using MySqlPool = chat::storage::MySqlPool;

struct UserInfo {
    std::string name;
    std::string pwd;
    int uid;
    std::string email;
    std::string password_fingerprint;
};

class MysqlDao {
  public:
    MysqlDao();
    ~MysqlDao();
    int RegUser(const std::string& name, const std::string& email, const std::string& pwd);
    int RegUserTransaction(const std::string& name, const std::string& email,
                           const std::string& pwd, const std::string& icon);
    bool CheckEmail(const std::string& name, const std::string& email,
                    bool* unavailable = nullptr);
    bool UpdatePwd(const std::string& name, const std::string& newpwd);
    bool UpdatePwdHash(const std::string& name, const std::string& email,
                       const std::string& encoded_hash);
    bool CheckPwd(const std::string& email, const std::string& pwd, UserInfo& userInfo,
                  bool* unavailable = nullptr);
    bool PasswordFingerprint(int uid, std::string& fingerprint, bool* unavailable = nullptr);

  private:
    void EnsurePasswordSchemeColumn();
    void MigrateLegacyPasswords();
    std::unique_ptr<MySqlPool> pool_;
};
