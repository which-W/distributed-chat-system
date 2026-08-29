#include "MysqlDao.h"
#include "FriendAcceptanceTransaction.h"
#include "ConfigMgr.h"
#include "PasswordHasher.h"
#include <sodium.h>

MysqlDao::MysqlDao()
{
	auto & cfg = ConfigMgr::Inst();
	const auto& host = cfg["Mysql"]["Host"];
	const auto& port = cfg["Mysql"]["Port"];
	const auto& pwd = cfg["Mysql"]["Passwd"];
	const auto& schema = cfg["Mysql"]["Schema"];
	const auto& user = cfg["Mysql"]["User"];
	pool_.reset(new MySqlPool(host+":"+port, user, pwd,schema, 5));
	EnsureFileTransferTable();
}

MysqlDao::~MysqlDao(){
	pool_->Close();
}

void MysqlDao::EnsureFileTransferTable()
{
	auto con = pool_->getConnection();
	if (!con) throw std::runtime_error("file transfer schema migration failed: database unavailable");
	Defer defer([this, &con]() { pool_->returnConnection(std::move(con)); });
	std::unique_ptr<sql::Statement> statement(con->_con->createStatement());
	statement->executeUpdate(
		"CREATE TABLE IF NOT EXISTS file_transfer ("
		"id CHAR(36) NOT NULL PRIMARY KEY, sender_uid INT NOT NULL, receiver_uid INT NOT NULL, "
		"original_name VARCHAR(255) NOT NULL, mime_type VARCHAR(128) NOT NULL, "
		"total_size BIGINT UNSIGNED NOT NULL, uploaded_size BIGINT UNSIGNED NOT NULL DEFAULT 0, "
		"sha256 CHAR(64) NOT NULL, status VARCHAR(16) NOT NULL, "
		"created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, completed_at TIMESTAMP NULL, "
		"downloaded_at TIMESTAMP NULL, expires_at TIMESTAMP NOT NULL, "
		"KEY idx_file_receiver (receiver_uid, status, expires_at), KEY idx_file_expiry (expires_at), "
		"CONSTRAINT fk_file_sender FOREIGN KEY (sender_uid) REFERENCES user(uid), "
		"CONSTRAINT fk_file_receiver FOREIGN KEY (receiver_uid) REFERENCES user(uid)) ENGINE=InnoDB");
}

int MysqlDao::RegUser(const std::string& name, const std::string& email, const std::string& pwd)
{
	auto con = pool_->getConnection();
	try {
		if (con == nullptr) {
			return false;
		}
		const auto password_hash = chat::security::PasswordHasher::hash(pwd);
		// 准备调用存储过程
		std::unique_ptr < sql::PreparedStatement > stmt(con->_con->prepareStatement("CALL reg_user(?,?,?,@result)"));
		// 设置输入参数
		stmt->setString(1, name);
		stmt->setString(2, email);
		stmt->setString(3, password_hash);

		// 由于PreparedStatement不直接支持注册输出参数，我们需要使用会话变量或其他方法来获取输出参数的值

		  // 执行存储过程
		stmt->execute();
		// 如果存储过程设置了会话变量或有其他方式获取输出参数的值，你可以在这里执行SELECT查询来获取它们
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

bool MysqlDao::CheckEmail(const std::string& name, const std::string& email) {
	auto con = pool_->getConnection();
	try {
		if (con == nullptr) {
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

bool MysqlDao::UpdatePwd(const std::string& name, const std::string& newpwd) {
	auto con = pool_->getConnection();
	try {
		if (con == nullptr) {
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

bool MysqlDao::CheckPwd(const std::string& name, const std::string& pwd, UserInfo& userInfo) {
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});

	try {
		// 准备SQL语句
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("SELECT * FROM user WHERE name = ?"));
		pstmt->setString(1, name); // 将username替换为你要查询的用户名

		// 执行查询
		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
		if (!res->next()) {
			return false;
		}
		const std::string stored_password = res->getString("pwd");
		bool valid = chat::security::PasswordHasher::verify(pwd, stored_password);
		const bool legacy = !chat::security::PasswordHasher::isEncodedHash(stored_password);
		if (legacy) {
			valid = pwd.size() == stored_password.size()
				&& sodium_memcmp(pwd.data(), stored_password.data(), pwd.size()) == 0;
		}
		if (!valid) return false;
		if (legacy || chat::security::PasswordHasher::needsRehash(stored_password)) {
			const auto upgraded = chat::security::PasswordHasher::hash(pwd);
			std::unique_ptr<sql::PreparedStatement> update(
				con->_con->prepareStatement("UPDATE user SET pwd = ? WHERE uid = ?"));
			update->setString(1, upgraded);
			update->setInt(2, res->getInt("uid"));
			update->executeUpdate();
		}
		userInfo.name = name;
		userInfo.email = res->getString("email");
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

bool MysqlDao::AddFriendApply(const int& from, const int& to)
{
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});

	try {
		// 准备SQL语句
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("INSERT INTO friend_apply (from_uid, to_uid) values (?,?) "
			"ON DUPLICATE KEY UPDATE from_uid = from_uid, to_uid = to_uid"));
		pstmt->setInt(1, from); // from id
		pstmt->setInt(2, to);
		// 执行更新
		int rowAffected = pstmt->executeUpdate();
		if (rowAffected < 0) {
			return false;
		}
		return true;
	}
	catch (sql::SQLException& e) {
		std::cerr << "SQLException: " << e.what();
		std::cerr << " (MySQL error code: " << e.getErrorCode();
		std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return false;
	}


	return true;
}

bool MysqlDao::AuthFriendApply(const int& from, const int& to) {
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});

	try {
		// 准备SQL语句
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("UPDATE friend_apply SET status = 1 "
			"WHERE from_uid = ? AND to_uid = ? AND status = 0"));
		//反过来的申请时from，验证时to
		pstmt->setInt(1, to); // from id
		pstmt->setInt(2, from);
		// 执行更新
		int rowAffected = pstmt->executeUpdate();
		return rowAffected == 1;
	}
	catch (sql::SQLException& e) {
		std::cerr << "SQLException: " << e.what();
		std::cerr << " (MySQL error code: " << e.getErrorCode();
		std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return false;
	}


	return true;
}

chat::storage::FriendAcceptanceResult MysqlDao::AddFriend(
	const int& from, const int& to, std::string back_name) {
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return chat::storage::FriendAcceptanceResult::StorageError;
	}
	Defer defer([this, &con]() { pool_->returnConnection(std::move(con)); });

	class MysqlFriendAcceptanceStore {
	public:
		explicit MysqlFriendAcceptanceStore(sql::Connection& connection) : connection_(connection) {}

		void begin() { connection_.setAutoCommit(false); }

		int insertFriend(int self_uid, int friend_uid, const std::string& back) {
			std::unique_ptr<sql::PreparedStatement> statement(connection_.prepareStatement(
				"INSERT IGNORE INTO friend(self_id, friend_id, back) VALUES (?, ?, ?)"));
			statement->setInt(1, self_uid);
			statement->setInt(2, friend_uid);
			statement->setString(3, back);
			return statement->executeUpdate();
		}

		int acceptPendingApplication(int applicant_uid, int accepter_uid) {
			std::unique_ptr<sql::PreparedStatement> statement(connection_.prepareStatement(
				"UPDATE friend_apply SET status = 1 "
				"WHERE from_uid = ? AND to_uid = ? AND status = 0"));
			statement->setInt(1, applicant_uid);
			statement->setInt(2, accepter_uid);
			return statement->executeUpdate();
		}

		void commit() { connection_.commit(); }
		void rollback() { connection_.rollback(); }

	private:
		sql::Connection& connection_;
	};

	try {
		// from 是接受者、to 是申请者；三项写入必须在同一事务内完成。
		MysqlFriendAcceptanceStore store(*con->_con);
		return chat::storage::ExecuteFriendAcceptanceTransaction(store, from, to, back_name);
	}
	catch (const sql::SQLException& error) {
		std::cerr << "Friend acceptance transaction failed: " << error.what()
			<< " (MySQL error code: " << error.getErrorCode()
			<< ", SQLState: " << error.getSQLState() << ")" << std::endl;
		return chat::storage::FriendAcceptanceResult::StorageError;
	}
}

std::shared_ptr<UserInfo> MysqlDao::GetUser(int uid)
{
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return nullptr;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});

	try {
		// 准备SQL语句
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("SELECT * FROM user WHERE uid = ?"));
		pstmt->setInt(1, uid); // 将uid替换为你要查询的uid

		// 执行查询
		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
		std::shared_ptr<UserInfo> user_ptr = nullptr;
		// 遍历结果集
		while (res->next()) {
			user_ptr.reset(new UserInfo);
			user_ptr->email = res->getString("email");
			user_ptr->name= res->getString("name");
			user_ptr->nick = res->getString("nick");
			user_ptr->desc = res->getString("desc");
			user_ptr->sex = res->getInt("sex");
			user_ptr->icon = res->getString("icon");
			user_ptr->uid = uid;
			break;
		}
		return user_ptr;
	}
	catch (sql::SQLException& e) {
		std::cerr << "SQLException: " << e.what();
		std::cerr << " (MySQL error code: " << e.getErrorCode();
		std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return nullptr;
	}
}

std::shared_ptr<UserInfo> MysqlDao::GetUser(std::string name)
{
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return nullptr;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});

	try {
		// 准备SQL语句
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("SELECT * FROM user WHERE name = ?"));
		pstmt->setString(1, name); // 将uid替换为你要查询的uid

		// 执行查询
		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
		std::shared_ptr<UserInfo> user_ptr = nullptr;
		// 遍历结果集
		while (res->next()) {
			user_ptr.reset(new UserInfo);
			user_ptr->email = res->getString("email");
			user_ptr->name = res->getString("name");
			user_ptr->nick = res->getString("nick");
			user_ptr->desc = res->getString("desc");
			user_ptr->sex = res->getInt("sex");
			user_ptr->uid = res->getInt("uid");
			break;
		}
		return user_ptr;
	}
	catch (sql::SQLException& e) {
		std::cerr << "SQLException: " << e.what();
		std::cerr << " (MySQL error code: " << e.getErrorCode();
		std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return nullptr;
	}
}


bool MysqlDao::GetApplyList(int touid, std::vector<std::shared_ptr<ApplyInfo>>& applyList, int begin, int limit) {
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});


		try {
		// 准备SQL语句, 根据起始id和限制条数返回列表
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("select apply.from_uid, apply.status, user.name, "
				"user.nick, user.sex from friend_apply as apply join user on apply.from_uid = user.uid where apply.to_uid = ? "
			"and apply.id > ? order by apply.id ASC LIMIT ? "));

		pstmt->setInt(1, touid); // 将uid替换为你要查询的uid
		pstmt->setInt(2, begin); // 起始id
		pstmt->setInt(3, limit); //偏移量
		// 执行查询
		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
		// 遍历结果集
		while (res->next()) {
			auto name = res->getString("name");
			auto uid = res->getInt("from_uid");
			auto status = res->getInt("status");
			auto nick = res->getString("nick");
			auto sex = res->getInt("sex");
			auto apply_ptr = std::make_shared<ApplyInfo>(uid, name, "", "", nick, sex, status);
			applyList.push_back(apply_ptr);
		}
		return true;
	}
	catch (sql::SQLException& e) {
		std::cerr << "SQLException: " << e.what();
		std::cerr << " (MySQL error code: " << e.getErrorCode();
		std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return false;
	}
}

bool MysqlDao::GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo> >& user_info_list) {

	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});


	try {
		// 准备SQL语句, 根据起始id和限制条数返回列表
		// 登录协议目前没有好友分页字段，先对单帧结果设置硬上限，防止无界列表溢出 16 位帧长。
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement(
			"select * from friend where self_id = ? order by friend_id LIMIT 200"));

		pstmt->setInt(1, self_id); // 将uid替换为你要查询的uid

		// 执行查询
		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
		// 遍历结果集
		while (res->next()) {
			auto friend_id = res->getInt("friend_id");
			auto back = res->getString("back");
			//再一次查询friend_id对应的信息
			auto user_info = GetUser(friend_id);
			if (user_info == nullptr) {
				continue;
			}

			user_info->back = user_info->name;
			user_info_list.push_back(user_info);
		}
		return true;
	}
	catch (sql::SQLException& e) {
		std::cerr << "SQLException: " << e.what();
		std::cerr << " (MySQL error code: " << e.getErrorCode();
		std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return false;
	}

	return true;
}

bool MysqlDao::AreFriends(int first_uid, int second_uid) {
	auto con = pool_->getConnection();
	if (con == nullptr) return false;
	Defer defer([this, &con]() { pool_->returnConnection(std::move(con)); });
	try {
		// 同时要求双向记录存在，避免好友创建事务只写入一半时放行私聊。
		std::unique_ptr<sql::PreparedStatement> statement(con->_con->prepareStatement(
			"SELECT COUNT(*) AS relation_count FROM friend "
			"WHERE (self_id = ? AND friend_id = ?) OR (self_id = ? AND friend_id = ?)"));
		statement->setInt(1, first_uid);
		statement->setInt(2, second_uid);
		statement->setInt(3, second_uid);
		statement->setInt(4, first_uid);
		std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
		return result->next() && result->getInt("relation_count") == 2;
	}
	catch (const sql::SQLException& error) {
		std::cerr << "Friend authorization query failed: " << error.what() << std::endl;
		return false;
	}
}

bool MysqlDao::HasPendingFriendApply(int from_uid, int to_uid) {
	auto con = pool_->getConnection();
	if (con == nullptr) return false;
	Defer defer([this, &con]() { pool_->returnConnection(std::move(con)); });
	try {
		std::unique_ptr<sql::PreparedStatement> statement(con->_con->prepareStatement(
			"SELECT 1 FROM friend_apply WHERE from_uid = ? AND to_uid = ? AND status = 0 LIMIT 1"));
		statement->setInt(1, from_uid);
		statement->setInt(2, to_uid);
		std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
		return result->next();
	}
	catch (const sql::SQLException& error) {
		std::cerr << "Friend application authorization query failed: " << error.what() << std::endl;
		return false;
	}
}

bool MysqlDao::CreateFileTransfer(const chat::files::TransferRecord& transfer)
{
	auto con = pool_->getConnection();
	if (!con) return false;
	Defer defer([this, &con]() { pool_->returnConnection(std::move(con)); });
	try {
		con->_con->setAutoCommit(false);
		// 锁住发送者账户，将配额检查和元数据插入串行化，防止并发请求同时越过上限。
		std::unique_ptr<sql::PreparedStatement> sender_lock(con->_con->prepareStatement(
			"SELECT uid FROM user WHERE uid=? FOR UPDATE"));
		sender_lock->setInt(1, transfer.sender_uid);
		std::unique_ptr<sql::ResultSet> sender(sender_lock->executeQuery());
		if (!sender->next()) {
			con->_con->rollback();
			con->_con->setAutoCommit(true);
			return false;
		}
		sender.reset(); sender_lock.reset();

		// 每个发送者最多保留 16 个、合计 1 GiB 的有效临时附件，避免耗尽共享磁盘。
		std::unique_ptr<sql::PreparedStatement> quota(con->_con->prepareStatement(
			"SELECT COUNT(*) AS item_count,COALESCE(SUM(total_size),0) AS total_bytes FROM file_transfer "
			"WHERE sender_uid=? AND expires_at>NOW() AND status IN('uploading','available','downloaded')"));
		quota->setInt(1, transfer.sender_uid);
		std::unique_ptr<sql::ResultSet> usage(quota->executeQuery());
		if (!usage->next() || usage->getUInt64("item_count") >= 16
			|| usage->getUInt64("total_bytes") + transfer.total_size > 1024ULL * 1024ULL * 1024ULL) {
			con->_con->rollback();
			con->_con->setAutoCommit(true);
			return false;
		}
		usage.reset(); quota.reset();
		std::unique_ptr<sql::PreparedStatement> statement(con->_con->prepareStatement(
			"INSERT INTO file_transfer(id,sender_uid,receiver_uid,original_name,mime_type,total_size,"
			"uploaded_size,sha256,status,expires_at) VALUES(?,?,?,?,?,?,0,?,'uploading',"
			"DATE_ADD(NOW(), INTERVAL 7 DAY))"));
		statement->setString(1, transfer.id); statement->setInt(2, transfer.sender_uid);
		statement->setInt(3, transfer.receiver_uid); statement->setString(4, transfer.original_name);
		statement->setString(5, transfer.mime_type); statement->setUInt64(6, transfer.total_size);
		statement->setString(7, transfer.sha256);
		const bool inserted = statement->executeUpdate() == 1;
		if (inserted) con->_con->commit(); else con->_con->rollback();
		con->_con->setAutoCommit(true);
		return inserted;
	} catch (const sql::SQLException& error) {
		try { con->_con->rollback(); con->_con->setAutoCommit(true); } catch (...) {}
		std::cerr << "Create file transfer failed: " << error.what() << std::endl;
		return false;
	}
}

std::optional<chat::files::TransferRecord> MysqlDao::GetFileTransfer(const std::string& id)
{
	auto con = pool_->getConnection();
	if (!con) return std::nullopt;
	Defer defer([this, &con]() { pool_->returnConnection(std::move(con)); });
	try {
		std::unique_ptr<sql::PreparedStatement> statement(con->_con->prepareStatement(
			"SELECT id,sender_uid,receiver_uid,original_name,mime_type,total_size,uploaded_size,sha256,status "
			"FROM file_transfer WHERE id=? AND expires_at>NOW()"));
		statement->setString(1, id);
		std::unique_ptr<sql::ResultSet> row(statement->executeQuery());
		if (!row->next()) return std::nullopt;
		chat::files::TransferRecord record;
		record.id=row->getString("id"); record.sender_uid=row->getInt("sender_uid");
		record.receiver_uid=row->getInt("receiver_uid"); record.original_name=row->getString("original_name");
		record.mime_type=row->getString("mime_type"); record.total_size=row->getUInt64("total_size");
		record.uploaded_size=row->getUInt64("uploaded_size"); record.sha256=row->getString("sha256");
		const auto status=row->getString("status");
		record.status=status=="uploading"?chat::files::TransferStatus::Uploading:
			status=="available"?chat::files::TransferStatus::Available:
			status=="downloaded"?chat::files::TransferStatus::Downloaded:
			status=="cancelled"?chat::files::TransferStatus::Cancelled:chat::files::TransferStatus::Expired;
		return record;
	} catch (const sql::SQLException& error) {
		std::cerr << "Get file transfer failed: " << error.what() << std::endl;
		return std::nullopt;
	}
}

std::vector<chat::files::TransferRecord> MysqlDao::GetPendingFileTransfers(int receiver_uid)
{
	std::vector<chat::files::TransferRecord> records;
	auto con = pool_->getConnection();
	if (!con) return records;
	Defer defer([this, &con]() { pool_->returnConnection(std::move(con)); });
	try {
		std::unique_ptr<sql::PreparedStatement> statement(con->_con->prepareStatement(
			"SELECT id FROM file_transfer WHERE receiver_uid=? AND status IN('available','downloaded') "
			"AND expires_at>NOW() ORDER BY created_at DESC LIMIT 200"));
		statement->setInt(1, receiver_uid);
		std::unique_ptr<sql::ResultSet> rows(statement->executeQuery());
		std::vector<std::string> ids;
		while (rows->next()) ids.push_back(rows->getString("id"));
		rows.reset(); statement.reset();
		for (const auto& id : ids) if (auto item=GetFileTransfer(id)) records.push_back(*item);
	} catch (const sql::SQLException& error) { std::cerr << "List file transfers failed: " << error.what() << std::endl; }
	return records;
}

bool MysqlDao::AdvanceFileUpload(const std::string& id, int sender_uid,
	std::uint64_t expected_offset, std::uint64_t new_offset)
{
	auto con=pool_->getConnection(); if(!con) return false;
	Defer defer([this,&con](){pool_->returnConnection(std::move(con));});
	try { std::unique_ptr<sql::PreparedStatement> s(con->_con->prepareStatement(
		"UPDATE file_transfer SET uploaded_size=? WHERE id=? AND sender_uid=? AND status='uploading' "
		"AND uploaded_size=? AND ?>uploaded_size AND ?<=total_size"));
		s->setUInt64(1,new_offset);s->setString(2,id);s->setInt(3,sender_uid);s->setUInt64(4,expected_offset);
		s->setUInt64(5,new_offset);s->setUInt64(6,new_offset);return s->executeUpdate()==1;
	} catch(const sql::SQLException&){return false;}
}

bool MysqlDao::CompleteFileTransfer(const std::string& id, int sender_uid)
{
	auto con=pool_->getConnection(); if(!con) return false; Defer defer([this,&con](){pool_->returnConnection(std::move(con));});
	try { std::unique_ptr<sql::PreparedStatement> s(con->_con->prepareStatement(
		"UPDATE file_transfer SET status='available',completed_at=NOW(),expires_at=DATE_ADD(NOW(),INTERVAL 7 DAY) "
		"WHERE id=? AND sender_uid=? AND status='uploading' AND uploaded_size=total_size"));
		s->setString(1,id);s->setInt(2,sender_uid);return s->executeUpdate()==1;} catch(const sql::SQLException&){return false;}
}

bool MysqlDao::MarkFileDownloaded(const std::string& id, int receiver_uid)
{
	auto con=pool_->getConnection(); if(!con) return false; Defer defer([this,&con](){pool_->returnConnection(std::move(con));});
	try { std::unique_ptr<sql::PreparedStatement> s(con->_con->prepareStatement(
		"UPDATE file_transfer SET status='downloaded',downloaded_at=COALESCE(downloaded_at,NOW()),"
		"expires_at=LEAST(expires_at,DATE_ADD(NOW(),INTERVAL 1 DAY)) WHERE id=? AND receiver_uid=? "
		"AND status IN('available','downloaded')"));s->setString(1,id);s->setInt(2,receiver_uid);return s->executeUpdate()==1;
	} catch(const sql::SQLException&){return false;}
}

bool MysqlDao::CancelFileTransfer(const std::string& id, int actor_uid)
{
	auto con=pool_->getConnection(); if(!con) return false; Defer defer([this,&con](){pool_->returnConnection(std::move(con));});
	try { std::unique_ptr<sql::PreparedStatement> s(con->_con->prepareStatement(
		"UPDATE file_transfer SET status='cancelled',expires_at=NOW() WHERE id=? AND (sender_uid=? OR receiver_uid=?) "
		"AND status IN('uploading','available','downloaded')"));s->setString(1,id);s->setInt(2,actor_uid);s->setInt(3,actor_uid);return s->executeUpdate()==1;
	} catch(const sql::SQLException&){return false;}
}

std::vector<std::string> MysqlDao::GetExpiredFileTransferIds()
{
	std::vector<std::string> ids;auto con=pool_->getConnection();if(!con)return ids;
	Defer defer([this,&con](){pool_->returnConnection(std::move(con));});
	try{std::unique_ptr<sql::Statement>s(con->_con->createStatement());
		std::unique_ptr<sql::ResultSet>rows(s->executeQuery(
			"SELECT id FROM file_transfer WHERE expires_at<=NOW() OR status IN('cancelled','expired') LIMIT 200"));
		while(rows->next())ids.push_back(rows->getString("id"));}catch(const sql::SQLException&){}
	return ids;
}

bool MysqlDao::DeleteFileTransfer(const std::string& id)
{
	auto con=pool_->getConnection();if(!con)return false;Defer defer([this,&con](){pool_->returnConnection(std::move(con));});
	try{std::unique_ptr<sql::PreparedStatement>s(con->_con->prepareStatement("DELETE FROM file_transfer WHERE id=? AND (expires_at<=NOW() OR status IN('cancelled','expired'))"));
		s->setString(1,id);return s->executeUpdate()==1;}catch(const sql::SQLException&){return false;}
}
