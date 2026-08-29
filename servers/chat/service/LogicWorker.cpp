#include "LogicWorker.h"
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include "FileSystem.h"
#include "EncryptedFileStore.h"
#include "MysqlMgr.h"
#include "UserMgr.h"
#include "RedisMgr.h"
#include "ChatGrpcClient.h"
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <sodium.h>

namespace {
EncryptedFileStore& fileStore()
{
	static EncryptedFileStore store(ConfigMgr::Inst()["FileStorage"]["Root"],
		ConfigMgr::Inst()["FileStorage"]["MasterKey"]);
	static std::mutex cleanupMutex;
	static auto lastCleanup = std::chrono::steady_clock::time_point::min();
	const auto now = std::chrono::steady_clock::now();
	std::lock_guard<std::mutex> cleanupLock(cleanupMutex);
	if (lastCleanup == std::chrono::steady_clock::time_point::min()
		|| now - lastCleanup >= std::chrono::minutes(10)) {
		// 清理动作先删除密文再删除元数据；失败记录会在下一轮继续重试。
		for (const auto& id : MysqlMgr::GetInstance()->GetExpiredFileTransferIds()) {
			store.remove(id);
			MysqlMgr::GetInstance()->DeleteFileTransfer(id);
		}
		lastCleanup = now;
	}
	return store;
}

bool validFileName(const std::string& name)
{
	if (name.empty() || name.size() > 255 || name == "." || name == ".."
		|| name.find('/') != std::string::npos || name.find('\\') != std::string::npos
		|| name.find_first_of("\r\n") != std::string::npos) return false;
	for (const unsigned char value : name) if (value < 0x20 || value == 0x7f) return false;
	return true;
}

std::vector<unsigned char> decodeBase64(const std::string& encoded)
{
	std::vector<unsigned char> decoded(encoded.size());
	std::size_t length = 0;
	if (sodium_base642bin(decoded.data(), decoded.size(), encoded.data(), encoded.size(),
		nullptr, &length, nullptr, sodium_base64_VARIANT_ORIGINAL) != 0) {
		throw std::invalid_argument("invalid base64 chunk");
	}
	decoded.resize(length);
	return decoded;
}

std::string encodeBase64(const std::vector<unsigned char>& data)
{
	std::string result(sodium_base64_ENCODED_LEN(data.size(), sodium_base64_VARIANT_ORIGINAL), '\0');
	sodium_bin2base64(result.data(), result.size(), data.data(), data.size(), sodium_base64_VARIANT_ORIGINAL);
	result.resize(std::strlen(result.c_str()));
	return result;
}

Json::Value transferJson(const chat::files::TransferRecord& item)
{
	Json::Value value;
	value["id"]=item.id; value["fromuid"]=item.sender_uid; value["touid"]=item.receiver_uid;
	value["name"]=item.original_name; value["mime"]=item.mime_type;
	value["total_size"]=Json::UInt64(item.total_size); value["sha256"]=item.sha256;
	return value;
}
}


LogicWorker::LogicWorker()
{
	RegisterCallBacks();

	_work_thread = std::thread([this]() {
		for (;;) {
			std::unique_lock<std::mutex> lock(_mtx);
			_cv.wait(lock, [this]() {
				if (_b_stop) {
					return true;
				}

				if (_task_que.empty()) {
					return false;
				}

				return true;

				});

			if (_b_stop) {
				return ;
			}

			auto task = _task_que.front();
			_task_que.pop();
			lock.unlock();
			try {
				task_callback(task);
			}
			catch (const std::exception& error) {
				std::cerr << "Rejected malformed upload message: " << error.what() << std::endl;
				task->getCSession()->Close();
			}
			catch (...) {
				std::cerr << "Rejected malformed upload message" << std::endl;
				task->getCSession()->Close();
			}
		}

		});
}

LogicWorker::~LogicWorker()
{
	{
		// 停止标志与任务队列共用同一把锁，避免析构线程和工作线程发生数据竞争。
		std::lock_guard<std::mutex> lock(_mtx);
		_b_stop = true;
	}
	_cv.notify_one();
	_work_thread.join();
}

bool LogicWorker::PostTask(std::shared_ptr<LogicNode> task)
{
	std::lock_guard<std::mutex> lock(_mtx);
	if (_b_stop || _task_que.size() >= MAX_MSG_QUEUE_SIZE) return false;
	_task_que.push(task);
	_cv.notify_one();
	return true;
}

void LogicWorker::RegisterCallBacks()
{
	_fun_callbacks[ID_UPLOAD_FILE_REQ] = [this](std::shared_ptr<CSession> session, const short&,
			const std::string& msg_data) {
				Json::Reader reader;
				Json::Value root;
				if (!reader.parse(msg_data, root) || !root.isObject() || !root["touid"].isInt()
					|| !root["name"].isString() || !root["total_size"].isUInt64()
					|| !root["sha256"].isString() || !root["mime"].isString()) {
					throw std::invalid_argument("invalid upload JSON");
				}
				const int sender=session->GetUserId(), receiver=root["touid"].asInt();
				const auto size=root["total_size"].asUInt64(); const auto name=root["name"].asString();
				const auto hash=root["sha256"].asString();
				const auto mime=root["mime"].asString();
				if (receiver<=0 || size>chat::files::MaxFileBytes || !validFileName(name)
					|| mime.size()>128 || hash.size()!=64
					|| hash.find_first_not_of("0123456789abcdef")!=std::string::npos
					|| !MysqlMgr::GetInstance()->AreFriends(sender,receiver)) {
					throw std::invalid_argument("rejected upload metadata");
				}
				std::string id=root.get("id","").asString();
				if (!id.empty()) {
					auto old=MysqlMgr::GetInstance()->GetFileTransfer(id);
					if (!old || old->sender_uid!=sender || old->receiver_uid!=receiver
						|| old->status!=chat::files::TransferStatus::Uploading
						|| old->original_name!=name || old->mime_type!=mime
						|| old->sha256!=hash || old->total_size!=size) throw std::invalid_argument("invalid resume");
					Json::Value response;response["error"]=0;response["id"]=id;
					response["offset"]=Json::UInt64(old->uploaded_size);session->Send(response.toStyledString(),ID_UPLOAD_FILE_RSP);return;
				}
				id=boost::uuids::to_string(boost::uuids::random_generator()());
				chat::files::TransferRecord item{id,sender,receiver,name,mime,size,0,hash};
				fileStore().create(id);
				if (!MysqlMgr::GetInstance()->CreateFileTransfer(item)){fileStore().remove(id);throw std::runtime_error("metadata create failed");}
				Json::Value response;response["error"]=0;response["id"]=id;response["offset"]=Json::UInt64(0);
				session->Send(response.toStyledString(),ID_UPLOAD_FILE_RSP);
			};

	_fun_callbacks[ID_UPLOAD_FILE_CHUNK_REQ]=[](std::shared_ptr<CSession> session,const short&,const std::string& body){
		Json::Value root;Json::Reader reader;if(!reader.parse(body,root)||!root["id"].isString()
			||!root["offset"].isUInt64()||!root["data"].isString())throw std::invalid_argument("invalid chunk JSON");
		auto item=MysqlMgr::GetInstance()->GetFileTransfer(root["id"].asString());
		const auto offset=root["offset"].asUInt64();auto data=decodeBase64(root["data"].asString());
		if(!item||item->sender_uid!=session->GetUserId()||item->status!=chat::files::TransferStatus::Uploading
			||item->uploaded_size!=offset||offset+data.size()>item->total_size)throw std::invalid_argument("invalid chunk state");
		const auto next=fileStore().append(item->id,offset,data);
		if(!MysqlMgr::GetInstance()->AdvanceFileUpload(item->id,item->sender_uid,offset,next)){
			MysqlMgr::GetInstance()->CancelFileTransfer(item->id,item->sender_uid);fileStore().remove(item->id);
			throw std::runtime_error("chunk metadata update failed");}
		Json::Value response;response["error"]=0;response["id"]=item->id;response["offset"]=Json::UInt64(next);
		session->Send(response.toStyledString(),ID_UPLOAD_FILE_CHUNK_RSP);
	};

	_fun_callbacks[ID_UPLOAD_FILE_FINISH_REQ]=[](std::shared_ptr<CSession> session,const short&,const std::string& body){
		Json::Value root;Json::Reader reader;if(!reader.parse(body,root)||!root["id"].isString())throw std::invalid_argument("invalid finish JSON");
		auto item=MysqlMgr::GetInstance()->GetFileTransfer(root["id"].asString());
		if(!item||item->sender_uid!=session->GetUserId()||item->uploaded_size!=item->total_size
			||fileStore().sha256(item->id,item->total_size)!=item->sha256)throw std::invalid_argument("file checksum mismatch");
		if(!MysqlMgr::GetInstance()->CompleteFileTransfer(item->id,item->sender_uid))throw std::runtime_error("complete failed");
		Json::Value response=transferJson(*item);response["error"]=0;session->Send(response.toStyledString(),ID_UPLOAD_FILE_FINISH_RSP);
		if(auto target=UserMgr::GetInstance()->GetSession(item->receiver_uid)) {
			target->Send(response.toStyledString(),ID_NOTIFY_FILE_REQ);
		} else {
			std::string server_name;
			if (RedisMgr::GetInstance()->Get(USERIPPREFIX + std::to_string(item->receiver_uid), server_name)
				&& server_name != ConfigMgr::Inst()["SelfServer"]["Name"]) {
				message::FileAvailableReq request;request.set_id(item->id);request.set_fromuid(item->sender_uid);
				request.set_touid(item->receiver_uid);request.set_name(item->original_name);
				request.set_mime(item->mime_type);request.set_total_size(item->total_size);request.set_sha256(item->sha256);
				ChatGrpcClient::GetInstance()->NotifyFileAvailable(server_name,request);
			}
		}
	};

	_fun_callbacks[ID_DOWNLOAD_FILE_REQ]=[](std::shared_ptr<CSession> session,const short&,const std::string& body){
		Json::Value root;Json::Reader reader;if(!reader.parse(body,root)||!root["id"].isString()||!root["offset"].isUInt64())throw std::invalid_argument("invalid download JSON");
		auto item=MysqlMgr::GetInstance()->GetFileTransfer(root["id"].asString());const auto offset=root["offset"].asUInt64();
		if(!item||item->receiver_uid!=session->GetUserId()||offset>item->total_size
			||(item->status!=chat::files::TransferStatus::Available&&item->status!=chat::files::TransferStatus::Downloaded))throw std::invalid_argument("download denied");
		Json::Value response=transferJson(*item);response["error"]=0;response["offset"]=Json::UInt64(offset);
		if(offset<item->total_size){auto chunk=fileStore().read(item->id,offset,chat::files::PlainChunkBytes);response["data"]=encodeBase64(chunk);response["next_offset"]=Json::UInt64(offset+chunk.size());}
		else response["complete"]=true;
		session->Send(response.toStyledString(),ID_DOWNLOAD_FILE_CHUNK);
	};

	_fun_callbacks[ID_DOWNLOAD_FILE_DONE]=[](std::shared_ptr<CSession> session,const short&,const std::string& body){
		Json::Value root;Json::Reader reader;if(!reader.parse(body,root)||!root["id"].isString())throw std::invalid_argument("invalid download completion");
		if(!MysqlMgr::GetInstance()->MarkFileDownloaded(root["id"].asString(),session->GetUserId()))throw std::invalid_argument("download completion denied");
	};
	_fun_callbacks[ID_FILE_TRANSFER_CANCEL]=[](std::shared_ptr<CSession> session,const short&,const std::string& body){
		Json::Value root;Json::Reader reader;if(!reader.parse(body,root)||!root["id"].isString())throw std::invalid_argument("invalid cancellation");
		if(MysqlMgr::GetInstance()->CancelFileTransfer(root["id"].asString(),session->GetUserId()))fileStore().remove(root["id"].asString());
	};
}

void LogicWorker::task_callback(std::shared_ptr<LogicNode> task)
{
	cout << "recv_msg id  is " << task->getRecvNode()->GetRecMsgNodeID() << endl;
	auto call_back_iter = _fun_callbacks.find(task->getRecvNode()->GetRecMsgNodeID());
	if (call_back_iter == _fun_callbacks.end()) {
		return;
	}
	call_back_iter->second(task->getCSession(), task->getRecvNode()->GetRecMsgNodeID(),
		std::string(task->getRecvNode()->_data, task->getRecvNode()->_cur_len));
}
