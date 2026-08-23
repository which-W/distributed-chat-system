#pragma once

#include <QObject>
#include <QPointer>

class AuthWindow;
class ChatWindow;

class ClientCoordinator final : public QObject
{
    Q_OBJECT

public:
    explicit ClientCoordinator(QObject* parent = nullptr);
    void start();

private slots:
    void showAuthentication();
    void showChat();

private:
    QPointer<AuthWindow> authWindow_;
    QPointer<ChatWindow> chatWindow_;
};
