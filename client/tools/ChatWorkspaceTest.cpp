#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QDebug>
#include <QFontDatabase>
#include <QEventLoop>
#include <cstdio>
#include "ChatWindow.h"
#include "ChatDialog.h"
#include "ChatPage.h"
#include "SearchList.h"
#include "ApplyFriendPage.h"
#include "ApplyFriend.h"
#include "FindSuccessWidght.h"
#include "MessageTextEdit.h"
#include "ThemeManager.h"
#include "ElaTheme.h"
#include "usermgr.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
    auto settle = []() { QEventLoop loop; QTimer::singleShot(350, &loop, &QEventLoop::quit); loop.exec(); };
    QCoreApplication::setOrganizationName("NebulaChatTests");
    QCoreApplication::setApplicationName("Workspace");
    ThemeManager::instance().initialize();
    auto user = UserMgr::Getinstance();
    user->SetUserInfo(std::make_shared<UserInfo>(1, QStringLiteral("我"), ":/res/head_1.jpg"));
    ChatWindow window;
    QFontDatabase::addApplicationFont(":/include/Font/ElaAwesome.ttf");
    auto* workspace = window.findChild<ChatDialog*>();
    auto* edit = workspace->findChild<MessageTextEdit*>();
    auto* search = workspace->findChild<QLineEdit*>("search_line");
    auto* results = workspace->findChild<SearchList*>();
    int failures = 0;
    auto check = [&failures](bool ok, const char* message) {
        if (!ok) { fprintf(stderr, "%s\n", message); ++failures; }
    };
    window.show();
    QDir().mkpath("ui-preview");
    settle();
    settle();
    app.processEvents();
    check(!edit->isEnabled(), "empty conversation must not allow sending");
    workspace->slot_side_contact();
    app.processEvents();
    check(workspace->findChild<QLabel*>("friendEmptyState")->isVisible(), "missing empty friends state");
    workspace->findChild<QPushButton*>("addFriendButton")->click();
    app.processEvents();
    check(results->isVisible(), "add friend must open search");
    search->setText("  ");
    results->search();
    check(!results->findChild<QTimer*>(), "blank search must not start request");
    search->setText("2002");
    results->search();
    check(results->findChild<QTimer*>()->isActive(), "search must have a timeout");
    check(workspace->findChildren<Loadingdlg*>().isEmpty(), "search must not show a GIF dialog");
    emit TcpMgr::Getinstance()->sig_user_search(nullptr);
    results->CloseFindDlg();
    check(!results->findChild<QTimer*>()->isActive(), "response must stop timeout");
    workspace->slot_side_contact();
    auto* requests = workspace->findChild<ApplyFriendPage*>();
    requests->AddNewApply(std::make_shared<AddFriendApply>(3, "Alex", "Hello", "", "Alex", 0));
    app.processEvents();
    auto* list = requests->findChild<ApplyFriendList*>();
    check(bool(list->item(0)->flags() & Qt::ItemIsEnabled), "friend request row must remain enabled");
    auto* accept = list->itemWidget(list->item(0))->findChild<QPushButton*>();
    check(accept && accept->isEnabled(), "accept request button must be usable");
    if (accept) accept->click();
    app.processEvents();
    auto* approval = requests->findChild<AuthenFriend*>();
    check(approval && approval->isVisible(), "accept button must open friend approval");
    if (approval) approval->grab().save("ui-preview/accept-friend.png");
    if (approval) approval->hide();
    check(!requests->findChild<QLabel*>("friendEmptyState")->isVisible(), "request must replace empty state");

    user->AppendFriendList(QJsonArray{QJsonObject{{"uid", 2}, {"name", QStringLiteral("萌新")},
        {"nick", QStringLiteral("萌新")}, {"icon", ":/res/head_2.jpg"}}});
    auto friendInfo = std::make_shared<UserInfo>(user->GetFriendById(2));
    auto* page = workspace->findChild<ChatPage*>();
    page->SetUserInfo(friendInfo);
    page->AppendChatMsg(std::make_shared<TextChatData>("a", QStringLiteral("嗨！最近在忙什么呢？"), 2, 1));
    page->AppendChatMsg(std::make_shared<TextChatData>("b", QStringLiteral("在做毕业设计，进展还不错～"), 1, 2));
    page->AppendChatMsg(std::make_shared<TextChatData>("c", QStringLiteral("有时间一起聊聊细节吗？"), 2, 1));
    emit FileTransferManager::Getinstance()->transferAvailable(QJsonObject{
        {"id", "preview-file"}, {"fromuid", 2}, {"name", QStringLiteral("项目界面设计说明与文件传输测试.pdf")}, {"total_size", 204800}});
    workspace->addChatUserList();
    workspace->SetSelectChatItem(2);
    workspace->slot_side_chat();
    QDir().mkpath("ui-preview");
    for (auto mode : {ElaThemeType::Light, ElaThemeType::Dark}) {
        eTheme->setThemeMode(mode);
        emit ThemeManager::instance().themeChanged(mode);
        for (const QSize size : {QSize(1000, 680), QSize(1280, 800)}) {
            window.resize(size);
            settle();
            app.processEvents();
            edit->setPlainText(QStringLiteral("输入消息测试\n第二行"));
            app.processEvents();
            check(edit->width() > 180 && edit->height() >= 48 && edit->height() <= 120,
                  "composer must fit resized window");
            auto* attachment = page->findChild<FileBubble*>();
            check(attachment && attachment->width() <= page->width(), "attachment must fit conversation");
            const QString name = QString("ui-preview/chat-%1-%2.png")
                .arg(mode == ElaThemeType::Light ? "light" : "dark").arg(size.width());
            check(window.grab().save(name), "cannot save preview");
        }
        workspace->slot_side_contact();
        app.processEvents();
        window.grab().save(QString("ui-preview/contacts-%1.png").arg(mode == ElaThemeType::Light ? "light" : "dark"));
        workspace->slot_side_chat();
    }
    ApplyFriend dialog(workspace);
    dialog.SetSearchInfo(std::make_shared<SearchInfo>(20, "Alex", "Alex", "", 0, ""));
    dialog.show();
    settle();
    app.processEvents();
    dialog.grab().save("ui-preview/add-friend.png");
    dialog.hide();
    qInfo() << "Workspace checks failed:" << failures;
    return failures ? 1 : 0;
}
