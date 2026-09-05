#pragma once
#include "const.h"
#include "MysqlDao.h"
class MysqlMgr: public Singleton<MysqlMgr>
{
	friend class Singleton<MysqlMgr>;
public:
	~MysqlMgr();
	int RegUser(const std::string& name, const std::string& email,  const std::string& pwd);
	bool CheckEmail(const std::string& name, const std::string & email);
	bool UpdatePwd(const std::string& name, const std::string& email);
	bool CheckPwd(const std::string& name, const std::string& pwd, UserInfo& userInfo);
	bool AddFriendApply(const int& from, const int& to);
	std::shared_ptr<UserInfo> GetUser(int uid);
	std::shared_ptr<UserInfo> GetUser(std::string name);
	bool GetApplyList(int touid, std::vector<std::shared_ptr<ApplyInfo>>& applyList, int begin, int limit = 10);
	bool GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo> >& user_info);
	bool AreFriends(int first_uid, int second_uid);
	bool HasPendingFriendApply(int from_uid, int to_uid);
	bool PersistTextMessages(const std::vector<chat::messages::TextMessage>& messages);
	std::vector<chat::messages::TextMessage> GetPendingTextMessages(
		int receiver_uid, int limit = 200);
	bool AcknowledgeTextMessages(int receiver_uid, int sender_uid,
		const std::vector<std::string>& client_message_ids);
	bool AuthFriendApply(const int& from, const int& to);
	chat::storage::FriendAcceptanceResult AddFriend(
		const int& from, const int& to, std::string back_name);
	bool CreateFileTransfer(const chat::files::TransferRecord& transfer);
	std::optional<chat::files::TransferRecord> GetFileTransfer(const std::string& id);
	std::vector<chat::files::TransferRecord> GetPendingFileTransfers(int receiver_uid);
	bool AdvanceFileUpload(const std::string& id, int sender_uid,
		std::uint64_t expected_offset, std::uint64_t new_offset);
	bool CompleteFileTransfer(const std::string& id, int sender_uid);
	bool MarkFileDownloaded(const std::string& id, int receiver_uid);
	bool CancelFileTransfer(const std::string& id, int actor_uid);
	std::vector<std::string> GetExpiredFileTransferIds();
	bool DeleteFileTransfer(const std::string& id);
private:
	MysqlMgr();
	MysqlDao  _dao;
};
