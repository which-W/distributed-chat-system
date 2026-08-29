#include "const.h"

#include <iostream>

int main()
{
    if (!IsClientRequestMessage(MSG_CHAT_LOGIN)
        || !IsClientRequestMessage(ID_TEXT_CHAT_MSG_REQ)
        || !IsClientRequestMessage(ID_TEXT_CHAT_MSG_ACK)
        || !IsClientRequestMessage(ID_DOWNLOAD_FILE_DONE)) {
        std::cerr << "合法客户端消息被协议白名单拒绝\n";
        return 1;
    }
    if (IsClientRequestMessage(ID_TEXT_CHAT_MSG_RSP)
        || IsClientRequestMessage(ID_NOTIFY_TEXT_CHAT_MSG_REQ)
        || IsClientRequestMessage(ID_HEARTBEAT_RSP)
        || IsClientRequestMessage(2048)) {
        std::cerr << "服务端响应或未知消息被当成客户端请求接受\n";
        return 1;
    }
    return 0;
}
