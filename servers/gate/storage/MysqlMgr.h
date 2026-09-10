#pragma once
#include "MysqlDao.h"
#include "Singleton.h"
#include "const.h"
#include <queue>
#include <thread>

class MysqlMgr : public Singleton<MysqlMgr> {
    friend class Singleton<MysqlMgr>;

  public:
    ~MysqlMgr();
    int RegUser(const std::string& name, const std::string& email, const std::string& pwd,
                const std::string& icon);
    bool CheckEmail(const std::string& name, const std::string& email);
    bool UpdatePwd(const std::string& name, const std::string& pwd);
    bool CheckPwd(const std::string& email, const std::string& pwd, UserInfo& userinfo,
                  bool* unavailable = nullptr);

  private:
    MysqlMgr();
    MysqlDao _Dao;
};
