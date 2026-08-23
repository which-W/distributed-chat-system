#include "MysqlDao.h"
#include "PasswordHasher.h"
#include <sodium.h>
#include <vector>

MysqlDao::MysqlDao()
{
    auto& cfg = ConfigMgr::ins();
    const auto& host = cfg["Mysql"]["Host"];
    const auto& port = cfg["Mysql"]["Port"];
    const auto& pwd = cfg["Mysql"]["Passwd"];
    const auto& schema = cfg["Mysql"]["Schema"];
    const auto& user = cfg["Mysql"]["User"];
    pool_.reset(new MySqlPool(host + ":" + port, user, pwd, schema, 5));
    MigrateLegacyPasswords();
}

MysqlDao::~MysqlDao() {
    pool_->Close();
}

void MysqlDao::MigrateLegacyPasswords()
{
    auto con = pool_->getConnection();
    if (con == nullptr) {
        return;
    }
    Defer defer([this, &con]() { pool_->returnConnection(std::move(con)); });

    try {
        std::vector<std::pair<int, std::string>> legacy_passwords;
        std::unique_ptr<sql::PreparedStatement> select(
            con->_con->prepareStatement("SELECT uid, pwd FROM user"));
        std::unique_ptr<sql::ResultSet> rows(select->executeQuery());
        while (rows->next()) {
            const std::string stored_password = rows->getString("pwd");
            if (!chat::security::PasswordHasher::isEncodedHash(stored_password)) {
                legacy_passwords.emplace_back(rows->getInt("uid"), stored_password);
            }
        }
        rows.reset();
        select.reset();

        int migrated = 0;
        for (const auto& [uid, plaintext] : legacy_passwords) {
            if (plaintext.empty()) {
                continue;
            }
            const auto encoded = chat::security::PasswordHasher::hash(plaintext);
            std::unique_ptr<sql::PreparedStatement> update(con->_con->prepareStatement(
                "UPDATE user SET pwd = ? WHERE uid = ? AND pwd = ?"));
            update->setString(1, encoded);
            update->setInt(2, uid);
            update->setString(3, plaintext);
            migrated += update->executeUpdate();
        }
        if (migrated > 0) {
            std::cout << "Migrated " << migrated << " legacy password row(s) to Argon2id" << std::endl;
        }
    }
    catch (const std::exception& error) {
        std::cerr << "Legacy password migration failed: " << error.what() << std::endl;
    }
}

int MysqlDao::RegUser(const std::string& name, const std::string& email, const std::string& pwd)
{
    auto con = pool_->getConnection();
    try {
        if (con == nullptr) {
            pool_->returnConnection(std::move(con));
            return false;
        }
        const auto password_hash = chat::security::PasswordHasher::hash(pwd);
        // 准备调用存储过程
        std::unique_ptr<sql::PreparedStatement>stmt(con->_con->prepareStatement("CALL reg_user(?,?,?,@result)"));
        // 设置输入参数
        stmt->setString(1, name);
        stmt->setString(2, email);
        stmt->setString(3, password_hash);
        // 由于PreparedStatement不直接支持注册输出参数，我们需要使用会话变量或其他方法来获取输出参数的值
          // 执行存储过程
        stmt->execute();
       // 例如，如果存储过程设置了一个会话变量@result来存储输出结果，可以这样获取：
        std::unique_ptr<sql::Statement> stmtResult(con->_con->createStatement());
        std::unique_ptr<sql::ResultSet> res(stmtResult->executeQuery("SELECT @result AS result"));
        if (res->next()) {
            int result = res->getInt("result");
            std::cout << "Result: " << result << std::endl;
            pool_->returnConnection(std::move(con));
            return result;
        }
        pool_->returnConnection(std::move(con));
        return -1;
    }
    catch (sql::SQLException& e) {
        pool_->returnConnection(std::move(con));
        std::cerr << "SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return -1;
    }
}

int MysqlDao::RegUserTransaction(const std::string& name, const std::string& email, const std::string& pwd,
    const std::string& icon)
{
    auto con = pool_->getConnection();
    if (con == nullptr) {
        return false;
    }

    Defer defer([this, &con] {
        pool_->returnConnection(std::move(con));
        });

    try {
        const auto password_hash = chat::security::PasswordHasher::hash(pwd);
        //开始事务
        con->_con->setAutoCommit(false);
        //执行第一个数据库操作，根据email查找用户
            // 准备查询语句

        std::unique_ptr<sql::PreparedStatement> pstmt_email(con->_con->prepareStatement("SELECT 1 FROM user WHERE email = ?"));

        // 绑定参数
        pstmt_email->setString(1, email);

        // 执行查询
        std::unique_ptr<sql::ResultSet> res_email(pstmt_email->executeQuery());

        auto email_exist = res_email->next();
        if (email_exist) {
            con->_con->rollback();
            std::cout << "email already exists";
            return 0;
        }

        // 准备查询用户名是否重复
        std::unique_ptr<sql::PreparedStatement> pstmt_name(con->_con->prepareStatement("SELECT 1 FROM user WHERE name = ?"));

        // 绑定参数
        pstmt_name->setString(1, name);

        // 执行查询
        std::unique_ptr<sql::ResultSet> res_name(pstmt_name->executeQuery());

        auto name_exist = res_name->next();
        if (name_exist) {
            con->_con->rollback();
            std::cout << "name " << name << " exist";
            return 0;
        }

        // 准备更新用户id
        std::unique_ptr<sql::PreparedStatement> pstmt_upid(con->_con->prepareStatement("UPDATE user_id SET id = id + 1"));

        // 执行更新
        pstmt_upid->executeUpdate();

        // 获取更新后的 id 值
        std::unique_ptr<sql::PreparedStatement> pstmt_uid(con->_con->prepareStatement("SELECT id FROM user_id"));
        std::unique_ptr<sql::ResultSet> res_uid(pstmt_uid->executeQuery());
        int newId = 0;
        // 处理结果集
        if (res_uid->next()) {
            newId = res_uid->getInt("id");
        }
        else {
            std::cout << "select id from user_id failed" << std::endl;
            con->_con->rollback();
            return -1;
        }

        // 插入user信息
        std::unique_ptr<sql::PreparedStatement> pstmt_insert(con->_con->prepareStatement("INSERT INTO user (uid, name, email, pwd, nick, icon) "
            "VALUES (?, ?, ?, ?,?,?)"));
        pstmt_insert->setInt(1, newId);
        pstmt_insert->setString(2, name);
        pstmt_insert->setString(3, email);
        pstmt_insert->setString(4, password_hash);
        pstmt_insert->setString(5, name);
        pstmt_insert->setString(6, icon);
        //执行插入
        pstmt_insert->executeUpdate();
        // 提交事务
        con->_con->commit();
        std::cout << "newuser insert into user success" << std::endl;
        return newId;
    }
    catch (sql::SQLException& e) {
        // 如果发生错误，回滚事务
        if (con) {
            con->_con->rollback();
        }
        std::cerr << "SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return -1;
    }
}

bool MysqlDao::CheckEmail(const std::string& name, const std::string& email)
{
    auto con = pool_->getConnection();
    try {
        if (con == nullptr) {
            pool_->returnConnection(std::move(con));
            return false;
        }
        // 准备查询语句
        std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("SELECT email FROM user WHERE name = ?"));
        // 绑定参数
        pstmt->setString(1, name);
        // 执行查询
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        // 遍历结果集
        while (res->next()) {
            std::cout << "Email lookup completed" << std::endl;
            if (email != res->getString("email")) {
                pool_->returnConnection(std::move(con));
                return false;
            }
            pool_->returnConnection(std::move(con));
            return true;
        }
        pool_->returnConnection(std::move(con));
        return false;
    }
    catch (sql::SQLException& e) {
        pool_->returnConnection(std::move(con));
        std::cerr << "SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return false;
    }
}

bool MysqlDao::UpdatePwd(const std::string& name, const std::string& newpwd)
{
    auto con = pool_->getConnection();
    try {
        if (con == nullptr) {
            pool_->returnConnection(std::move(con));
            return false;
        }
        const auto password_hash = chat::security::PasswordHasher::hash(newpwd);
        // 准备查询语句
        std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("UPDATE user SET pwd = ? WHERE name = ?"));
        // 绑定参数
        pstmt->setString(2, name);
        pstmt->setString(1, password_hash);
        // 执行更新
        int updateCount = pstmt->executeUpdate();
        std::cout << "Updated rows: " << updateCount << std::endl;
        pool_->returnConnection(std::move(con));
        return true;
    }
    catch (sql::SQLException& e) {
        pool_->returnConnection(std::move(con));
        std::cerr << "SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return false;
    }
}

bool MysqlDao::CheckPwd(const std::string& email, const std::string& pwd, UserInfo& userInfo) {
    auto con = pool_->getConnection();
    Defer defer([this, &con]() {
        pool_->returnConnection(std::move(con));
        });
    try {
        if (con == nullptr) {
            return false;
        }
        // 准备SQL语句
        std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("SELECT name,uid,pwd FROM user WHERE email = ?"));
        pstmt->setString(1, email); // 将email替换掉
        // 执行查询
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        if (!res->next()) {
            return false;
        }
        const std::string stored_password = res->getString("pwd");
        bool password_valid = chat::security::PasswordHasher::verify(pwd, stored_password);
        const bool legacy_plaintext = !chat::security::PasswordHasher::isEncodedHash(stored_password);
        if (legacy_plaintext) {
            password_valid = pwd.size() == stored_password.size()
                && sodium_memcmp(pwd.data(), stored_password.data(), pwd.size()) == 0;
        }
        if (!password_valid) {
            return false;
        }

        if (legacy_plaintext || chat::security::PasswordHasher::needsRehash(stored_password)) {
            const auto upgraded_hash = chat::security::PasswordHasher::hash(pwd);
            std::unique_ptr<sql::PreparedStatement> upgrade(
                con->_con->prepareStatement("UPDATE user SET pwd = ? WHERE uid = ?"));
            upgrade->setString(1, upgraded_hash);
            upgrade->setInt(2, res->getInt("uid"));
            upgrade->executeUpdate();
        }
        userInfo.name = res->getString("name");
        userInfo.email = email;
        userInfo.uid = res->getInt("uid");
        userInfo.pwd.clear();
        return true;
    }
    catch (sql::SQLException& e) {
        std::cerr << "SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return false;
    }
}
