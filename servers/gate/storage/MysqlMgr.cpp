#include "MysqlMgr.h"

MysqlMgr::MysqlMgr() {}

MysqlMgr::~MysqlMgr() {}

int MysqlMgr::RegUser(const std::string& name, const std::string& email, const std::string& pwd,
                      const std::string& icon) {
    return _Dao.RegUserTransaction(name, email, pwd, icon);
}

bool MysqlMgr::CheckEmail(const std::string& name, const std::string& email, bool* unavailable) {
    return _Dao.CheckEmail(name, email, unavailable);
}

bool MysqlMgr::UpdatePwd(const std::string& name, const std::string& pwd) {
    return _Dao.UpdatePwd(name, pwd);
}

bool MysqlMgr::CheckPwd(const std::string& email, const std::string& pwd, UserInfo& userinfo,
                        bool* unavailable) {
    return _Dao.CheckPwd(email, pwd, userinfo, unavailable);
}

bool MysqlMgr::UpdatePwdHash(const std::string& name, const std::string& email,
                             const std::string& encoded_hash) {
    return _Dao.UpdatePwdHash(name, email, encoded_hash);
}

bool MysqlMgr::PasswordFingerprint(int uid, std::string& fingerprint, bool* unavailable) {
    return _Dao.PasswordFingerprint(uid, fingerprint, unavailable);
}
