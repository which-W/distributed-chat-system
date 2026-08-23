#include "ClientCoordinator.h"

#include "AuthWindow.h"
#include "ChatWindow.h"
#include "TcpMgr.h"
#include "usermgr.h"

ClientCoordinator::ClientCoordinator(QObject* parent)
    : QObject(parent)
{
}

void ClientCoordinator::start()
{
    showAuthentication();
}

void ClientCoordinator::showAuthentication()
{
    if (!authWindow_) {
        authWindow_ = new AuthWindow;
        authWindow_->setAttribute(Qt::WA_DeleteOnClose);
        connect(authWindow_, &AuthWindow::authenticationSucceeded,
                this, &ClientCoordinator::showChat);
    }
    authWindow_->show();
    authWindow_->raise();

    if (chatWindow_) {
        TcpMgr::Getinstance()->slot_disconnect();
        UserMgr::Getinstance()->ResetSession();
        chatWindow_->hide();
        chatWindow_->deleteLater();
        chatWindow_.clear();
    }
}

void ClientCoordinator::showChat()
{
    if (!chatWindow_) {
        chatWindow_ = new ChatWindow;
        chatWindow_->setAttribute(Qt::WA_DeleteOnClose);
        connect(chatWindow_, &ChatWindow::logoutRequested,
                this, &ClientCoordinator::showAuthentication);
    }
    chatWindow_->show();
    chatWindow_->raise();

    if (authWindow_) {
        authWindow_->hide();
        authWindow_->deleteLater();
        authWindow_.clear();
    }
}
