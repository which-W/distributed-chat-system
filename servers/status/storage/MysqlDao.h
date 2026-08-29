#pragma once
#include "const.h"
#include "MysqlConnectionPool.h"
#include <memory>
#include <string>

using SqlConnection = chat::storage::SqlConnection;
using MySqlPool = chat::storage::MySqlPool;

struct UserInfo {
	std::string name;
	std::string pwd;
	int uid;
	std::string email;
};

class MysqlDao
{
public:
	MysqlDao();
	~MysqlDao();
	int RegUser(const std::string& name, const std::string& email, const std::string& pwd);
	bool CheckEmail(const std::string& name, const std::string & email);
	bool UpdatePwd(const std::string& name, const std::string& newpwd);
	bool CheckPwd(const std::string& name, const std::string& pwd, UserInfo& userInfo);
private:
	std::unique_ptr<MySqlPool> pool_;
};
