#include "MysqlMgr.h"

MysqlMgr::MysqlMgr()
{
}

MysqlMgr::~MysqlMgr()
{
}

int MysqlMgr::RegUser(const std::string& name, const std::string& email, const std::string& pwd , const std::string& icon)
{
    return _Dao.RegUserTransaction(name, email, pwd, icon);
}

bool MysqlMgr::CheckEmail(const std::string& name, const std::string& email) {
    return _Dao.CheckEmail(name, email);
}

bool MysqlMgr::UpdatePwd(const std::string& name, const std::string& pwd) {
    return _Dao.UpdatePwd(name, pwd);
}

bool MysqlMgr::CheckPwd(const std::string& email, const std::string& pwd, UserInfo& userinfo)
{
    return _Dao.CheckPwd(email, pwd, userinfo);
}
