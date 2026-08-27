#include "MysqlMgr.h"


MysqlMgr::~MysqlMgr() {

}

int MysqlMgr::RegUser(const std::string& name, const std::string& email, const std::string& pwd)
{
	return _dao.RegUser(name, email, pwd);
}

bool MysqlMgr::CheckEmail(const std::string& name, const std::string& email) {
	return _dao.CheckEmail(name, email);
}

bool MysqlMgr::UpdatePwd(const std::string& name, const std::string& pwd) {
	return _dao.UpdatePwd(name, pwd);
}

MysqlMgr::MysqlMgr() {
}

bool MysqlMgr::CheckPwd(const std::string& name, const std::string& pwd, UserInfo& userInfo) {
	return _dao.CheckPwd(name, pwd, userInfo);
}

bool MysqlMgr::AddFriendApply(const int& from, const int& to)
{
	return _dao.AddFriendApply(from , to);
}

bool MysqlMgr::AuthFriendApply(const int& from, const int& to)
{
	return _dao.AuthFriendApply(from , to);
}

chat::storage::FriendAcceptanceResult MysqlMgr::AddFriend(
	const int& from, const int& to, std::string back_name)
{
	return _dao.AddFriend(from ,to ,back_name);
}

std::shared_ptr<UserInfo> MysqlMgr::GetUser(int uid)
{
	return _dao.GetUser(uid);
}

std::shared_ptr<UserInfo> MysqlMgr::GetUser(std::string name)
{
	return _dao.GetUser(name);
}

bool MysqlMgr::GetApplyList(int touid, std::vector<std::shared_ptr<ApplyInfo>>& applyList, int begin, int limit)
{
	return _dao.GetApplyList(touid , applyList , begin , limit);
}

bool MysqlMgr::GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo>>& user_info)
{
	return _dao.GetFriendList(self_id , user_info);
}

bool MysqlMgr::AreFriends(int first_uid, int second_uid)
{
	return _dao.AreFriends(first_uid, second_uid);
}

bool MysqlMgr::HasPendingFriendApply(int from_uid, int to_uid)
{
	return _dao.HasPendingFriendApply(from_uid, to_uid);
}

bool MysqlMgr::CreateFileTransfer(const chat::files::TransferRecord& value){return _dao.CreateFileTransfer(value);}
std::optional<chat::files::TransferRecord> MysqlMgr::GetFileTransfer(const std::string& id){return _dao.GetFileTransfer(id);}
std::vector<chat::files::TransferRecord> MysqlMgr::GetPendingFileTransfers(int uid){return _dao.GetPendingFileTransfers(uid);}
bool MysqlMgr::AdvanceFileUpload(const std::string& id,int uid,std::uint64_t from,std::uint64_t to){return _dao.AdvanceFileUpload(id,uid,from,to);}
bool MysqlMgr::CompleteFileTransfer(const std::string& id,int uid){return _dao.CompleteFileTransfer(id,uid);}
bool MysqlMgr::MarkFileDownloaded(const std::string& id,int uid){return _dao.MarkFileDownloaded(id,uid);}
bool MysqlMgr::CancelFileTransfer(const std::string& id,int uid){return _dao.CancelFileTransfer(id,uid);}
std::vector<std::string> MysqlMgr::GetExpiredFileTransferIds(){return _dao.GetExpiredFileTransferIds();}
bool MysqlMgr::DeleteFileTransfer(const std::string& id){return _dao.DeleteFileTransfer(id);}
