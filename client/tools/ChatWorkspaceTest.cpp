#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QDebug>
#include <QFontDatabase>
#include <QEventLoop>
#include <QTcpServer>
#include <QTcpSocket>
#include <QDataStream>
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
#include "FriendInfoPage.h"
#include "RegisterDialog.h"
#include "LoginDialog.h"
#include <QStackedWidget>

int runResourceBoundaryTests();

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (app.arguments().contains("--resource-only")) return runResourceBoundaryTests();
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& message) {
        if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
            fprintf(stderr, "%s\n", message.toUtf8().constData());
    });
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
    auto settle = []() { QEventLoop loop; QTimer::singleShot(350, &loop, &QEventLoop::quit); loop.exec(); };
    QCoreApplication::setOrganizationName("NebulaChatTests");
    QCoreApplication::setApplicationName("Workspace");
    ThemeManager::instance().initialize();
    auto user = UserMgr::Getinstance();
    user->SetUserInfo(std::make_shared<UserInfo>(1, QStringLiteral("我"), ":/res/head_1.jpg"));
    int failures = 0;
    auto check = [&failures](bool ok, const char* message) {
        if (!ok) { fprintf(stderr, "%s\n", message); ++failures; }
    };
    {
        RegisterDialog registration;
        auto* pages = registration.findChild<QStackedWidget*>("stackedWidget");
        auto* form = pages->currentWidget();
        registration.slot_req_mod_finished(Req::ID_GET_VERIFT_CODE, "{\"error\":0}", ERR_OK);
        check(pages->currentWidget() == form, "verification response must not complete registration");
        registration.slot_req_mod_finished(Req::ID_REQ_USER, "{}", ERR_OK);
        check(pages->currentWidget() == form, "missing error must not complete registration");
        registration.slot_req_mod_finished(Req::ID_REQ_USER, "{\"error\":1004}", ERR_OK);
        check(pages->currentWidget() == form, "rejected code must not complete registration");
        registration.slot_req_mod_finished(Req::ID_REQ_USER, "{\"error\":0}", ERR_OK);
        check(pages->currentWidget() != form, "successful registration must show completion page");

        LoginDialog login;
        auto* button = login.findChild<QPushButton*>("login_button");
        button->setEnabled(false);
        Httpmgr::Getinstance()->slot_http_finished(Req::ID_LOGIN_USER, {}, ERR_NETWORK, Modules::LODINMOD);
        check(button->isEnabled(), "HTTP network failure must reach login and re-enable retry");
        button->setEnabled(false);
        login.slot_login_mod_finish(Req::ID_LOGIN_USER, "{}", ERR_OK);
        check(button->isEnabled(), "malformed login response must allow retry");
        button->setEnabled(false);
        login.slot_login_failed(ERR_LOCAL_STORAGE);
        check(button->isEnabled(), "local database failure must allow retry");
    }
    if (app.arguments().contains("--auth-only"))
        return failures ? 1 : 0;
    // 登录回归只需认证控件，聊天工作区延后到工作区测试时创建。
    ChatWindow window;
    QFontDatabase::addApplicationFont(":/include/Font/ElaAwesome.ttf");
    auto* workspace = window.findChild<ChatDialog*>();
    auto* edit = workspace->findChild<MessageTextEdit*>();
    auto* search = workspace->findChild<QLineEdit*>("search_line");
    auto* results = workspace->findChild<SearchList*>();
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
        {"id", "preview-file"}, {"fromuid", 2}, {"touid", 1}, {"name", QStringLiteral("项目界面设计说明与文件传输测试.pdf")}, {"total_size", 204800}});
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
    // Preview must decode a local image and remain available after completion.
    QImage sample(640, 360, QImage::Format_RGB32);
    sample.fill(QColor("#3278f6"));
    {
        QPainter painter(&sample);
        painter.setBrush(QColor("#adcfff"));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(400, 30, 130, 130);
        painter.setBrush(QColor("#153c83"));
        painter.drawPolygon(QPolygon{QPoint(0,360), QPoint(220,100), QPoint(460,360)});
        painter.setBrush(QColor("#e2edff"));
        painter.drawPolygon(QPolygon{QPoint(250,360), QPoint(470,170), QPoint(640,360)});
    }
    const auto imagePath = QDir::current().absoluteFilePath("ui-preview/preview-fixture.png");
    check(sample.save(imagePath), "cannot write image preview fixture");
    FileBubble imageBubble({{"id", "image-fixture"}, {"name", "旅行照片.png"}, {"total_size", 40960}}, ChatRole::Self, false);
    imageBubble.setLocalPreview(imagePath);
    imageBubble.setFinished(imagePath);
    imageBubble.resize(360, imageBubble.sizeHint().height());
    imageBubble.show();
    settle();
    auto* thumbnail = imageBubble.findChild<QPushButton*>("imageThumbnail");
    check(thumbnail && !thumbnail->isHidden() && !thumbnail->icon().isNull(), "completed image must show a thumbnail");
    check(imageBubble.findChild<QProgressBar*>()->isHidden(), "completed attachment must hide progress");
    thumbnail->click();
    settle();
    auto* imageDialog = imageBubble.findChild<QDialog*>("imagePreviewDialog");
    check(imageDialog && imageDialog->isVisible(), "thumbnail click must open image preview");
    if (imageDialog) imageDialog->close();
    for (auto mode : {ElaThemeType::Light, ElaThemeType::Dark}) {
        ThemeManager::instance().setThemeMode(mode);
        settle();
        imageBubble.grab().save(QString("ui-preview/image-card-%1.png").arg(mode == ElaThemeType::Light ? "light" : "dark"));
        workspace->slot_side_contact();
        workspace->slot_switch_friend_info_page(friendInfo);
        settle();
        window.grab().save(QString("ui-preview/profile-%1.png").arg(mode == ElaThemeType::Light ? "light" : "dark"));
    }
    imageBubble.setLocalPreview(imagePath + ".missing");
    check(thumbnail->isHidden(), "unreadable image must fall back to file card");
    imageBubble.hide();
    ApplyFriend dialog(workspace);
    dialog.SetSearchInfo(std::make_shared<SearchInfo>(20, "Alex", "Alex", "", 0, ""));
    dialog.show();
    settle();
    app.processEvents();
    dialog.grab().save("ui-preview/add-friend.png");
    dialog.hide();
    // Exercise real framed TCP input, including recovery without a live push.
    QTcpServer server;
    check(server.listen(QHostAddress::LocalHost, 0), "cannot start protocol fixture");
    QTcpSocket* peer = nullptr;
    QByteArray received;
    QJsonObject lastRequest;
    int lastId = 0;
    auto sendFrame = [&](Req id, const QJsonObject& value) {
        if (!peer) { check(false, "missing protocol connection"); return; }
        const auto payload = QJsonDocument(value).toJson(QJsonDocument::Compact);
        QByteArray frame;
        QDataStream out(&frame, QIODevice::WriteOnly);
        out << quint16(id) << quint16(payload.size());
        frame += payload;
        peer->write(frame);
    };
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&]() {
        peer = server.nextPendingConnection();
        QObject::connect(peer, &QTcpSocket::readyRead, &app, [&]() {
            received += peer->readAll();
            while (received.size() >= 4) {
                QDataStream in(received);
                quint16 id, length;
                in >> id >> length;
                if (received.size() < length + 4) return;
                lastId = id;
                lastRequest = QJsonDocument::fromJson(received.mid(4, length)).object();
                received.remove(0, length + 4);
                if (id == Req::ID_CHAT_LOGIN)
                    sendFrame(Req::ID_CHAT_LOGIN_RSP, QJsonObject{{"error", 0}, {"uid", 1}, {"name", "Tester"}});
            }
        });
    });
    auto tcp = TcpMgr::Getinstance();
    const auto stateConnection = QObject::connect(tcp.get(), &TcpMgr::sig_connection_state, &app,
        [](const QString& message, bool connected) { if (!connected) fprintf(stderr, "Connection: %s\n", message.toUtf8().constData()); });
    const auto loginConnection = QObject::connect(tcp.get(), &TcpMgr::sig_con_success, &app, [&](bool ok) {
        if (ok) tcp->slot_send_data(Req::ID_CHAT_LOGIN, QByteArrayLiteral("{\"uid\":1,\"token\":\"fixture\"}"));
    });
    ServerInfo endpoint;
    endpoint.Host = "127.0.0.1";
    endpoint.Port = QString::number(server.serverPort());
    endpoint.Transport = "insecure";
    endpoint.AllowInsecure = true;
    endpoint.Uid = 1;
    tcp->slot_tcp_connect(endpoint);
    settle();
    settle();
    dialog.show();
    auto* sendButton = dialog.findChild<QPushButton*>("dialogPrimary");
    sendButton->click();
    settle();
    check(lastId == Req::ID_ADD_FRIEND_REQ && lastRequest["touid"].toInt() == 20,
          "friend dialog must send the selected UID through TCP");
    check(!lastRequest["request_id"].toString().isEmpty(), "friend request must have a correlation ID");
    sendFrame(Req::ID_ADD_FRIEND_RSP, {{"error", 1}, {"request_id", lastRequest["request_id"]}});
    settle();
    check(dialog.isVisible() && sendButton->isEnabled(), "server failure must keep invitation retryable");
    sendButton->click();
    settle();
    sendFrame(Req::ID_ADD_FRIEND_RSP, {{"error", 0}, {"request_id", "stale-response"}});
    settle();
    check(sendButton->text() == QStringLiteral("正在发送…"), "stale reply must not complete a newer request");
    sendFrame(Req::ID_ADD_FRIEND_RSP, {{"error", 0}, {"request_id", lastRequest["request_id"]}});
    settle();
    check(!sendButton->isEnabled() && sendButton->text() == QStringLiteral("申请已发送"),
          "confirmed invitation must show success and prevent duplicate submission");
    dialog.hide();
    const QJsonObject application{{"uid", 31}, {"name", "Morgan"}, {"nick", "Morgan"}, {"status", 0}, {"icon", ":/res/head_1.jpg"}};
    const QJsonObject snapshot{{"apply_list", QJsonArray{application}}};
    sendFrame(Req::ID_HEARTBEAT_RSP, snapshot);
    settle();
    const auto applicationCount = user->GetApplyList().size();
    sendFrame(Req::ID_HEARTBEAT_RSP, snapshot);
    settle();
    check(user->isAlreadyApply(31) && user->GetApplyList().size() == applicationCount,
          "durable snapshot must restore missed invitation without duplicates");
    check(!window.findChild<QLabel*>("friendRequestBadge")->isHidden(), "recovered request must have a visible navigation badge");
    AuthenFriend acceptDialog(workspace);
    acceptDialog.SetApplyInfo(std::make_shared<ApplyInfo>(31, "Morgan", "", ":/res/head_1.jpg", "Morgan", 0, 0));
    acceptDialog.show();
    acceptDialog.findChild<QPushButton*>("dialogPrimary")->click();
    settle();
    check(lastId == Req::ID_AUTH_FRIEND_REQ && lastRequest["touid"].toInt() == 31,
          "acceptance must address the applicant UID");
    sendFrame(Req::ID_AUTH_FRIEND_RSP, {{"error", 0}, {"uid", 31}, {"name", "Morgan"},
        {"request_id", lastRequest["request_id"]}});
    settle();
    check(user->CheckFriendById(31), "acceptance must add the friend to the model");
    workspace->LoadMoreConWid();
    int matchingContacts = 0;
    auto* contacts = workspace->findChild<ContactUserList*>();
    for (int row = 0; row < contacts->count(); ++row) {
        auto* item = qobject_cast<ConUserItem*>(contacts->itemWidget(contacts->item(row)));
        if (item && item->GetInfo() && item->GetInfo()->_uid == 31) ++matchingContacts;
    }
    check(matchingContacts == 1, "pagination after accepting must not duplicate a contact");
    for (const auto& item : user->GetApplyList())
        if (item->_uid == 31) check(item->_status == 1, "accepted invitation must leave pending state");
    acceptDialog.hide();
    // Receive through the actual wire parser while a different conversation is open.
    page->SetUserInfo(friendInfo);
    const QJsonObject incomingFile{{"id", "wire-incoming-file"}, {"fromuid", 31}, {"touid", 1},
        {"name", "received.png"}, {"mime", "image/png"}, {"total_size", 512}, {"error", 0}};
    sendFrame(Req::ID_NOTIFY_FILE_REQ, incomingFile);
    settle();
    auto findFile = [&](const QString& id) -> FileBubble* {
        for (auto* bubble : page->findChildren<FileBubble*>())
            if (bubble->transferId() == id) return bubble;
        return nullptr;
    };
    check(!findFile("wire-incoming-file"), "incoming file must not appear in another conversation");
    check(user->GetFriendById(31)->_last_msg.contains("received.png"), "file notification must update conversation summary");
    page->SetUserInfo(std::make_shared<UserInfo>(user->GetFriendById(31)));
    settle();
    check(findFile("wire-incoming-file"), "switching to sender must restore incoming file");
    auto missedFile = incomingFile;
    missedFile["id"] = "wire-missed-file";
    auto sentFile = incomingFile;
    sentFile["id"] = "wire-sent-file";
    sentFile["fromuid"] = 1;
    sentFile["touid"] = 31;
    const QJsonObject fileSnapshot{{"pending_files", QJsonArray{missedFile, sentFile}}};
    sendFrame(Req::ID_HEARTBEAT_RSP, fileSnapshot);
    settle();
    check(findFile("wire-missed-file"), "heartbeat must recover a lost attachment notification");
    auto* restoredSent = findFile("wire-sent-file");
    check(restoredSent && restoredSent->findChild<QProgressBar*>()->isHidden(), "sent attachment must recover as completed");
    const int fileCount = page->findChildren<FileBubble*>().size();
    sendFrame(Req::ID_HEARTBEAT_RSP, fileSnapshot);
    settle();
    check(page->findChildren<FileBubble*>().size() == fileCount, "repeated snapshots must not duplicate attachments");
    auto transfers = FileTransferManager::Getinstance();
    QString failedTransfer;
    const auto failedConnection = QObject::connect(transfers.get(), &FileTransferManager::transferFailed,
        &app, [&](const QString& id, const QString&) { failedTransfer = id; });
    const auto rejectedUpload = transfers->startUpload(imagePath, 31);
    settle();
    check(!rejectedUpload.isEmpty() && lastId == Req::ID_UPLOAD_FILE_REQ, "new upload must send initialization");
    sendFrame(Req::ID_UPLOAD_FILE_RSP, {{"error", 1}});
    settle();
    check(failedTransfer == rejectedUpload, "ID-less initialization failure must identify the local bubble");
    const auto retryUpload = transfers->startUpload(imagePath, 31);
    settle();
    check(!retryUpload.isEmpty(), "rejected upload must release the slot for a new file");
    auto* uploadTimer = transfers->findChild<QTimer*>("uploadAckTimer");
    check(uploadTimer && uploadTimer->isActive(), "upload must have a response deadline");
    if (uploadTimer) QMetaObject::invokeMethod(uploadTimer, "timeout", Qt::DirectConnection);
    check(failedTransfer == retryUpload, "unacknowledged upload must fail with the correct local token");
    const auto nextUpload = transfers->startUpload(imagePath, 31);
    check(!nextUpload.isEmpty(), "timeout must not block the next upload");
    transfers->cancel(nextUpload);
    QObject::disconnect(failedConnection);
    for (auto mode : {ElaThemeType::Light, ElaThemeType::Dark}) {
        eTheme->setThemeMode(mode);
        emit ThemeManager::instance().themeChanged(mode);
        FindSuccessWidght found(workspace);
        found.SetSearchInfo(std::make_shared<SearchInfo>(31, "Morgan", "Morgan", "", 0, ":/res/head_1.jpg"));
        found.show();
        settle();
        found.grab().save(QString("ui-preview/found-friend-%1.png").arg(mode == ElaThemeType::Light ? "light" : "dark"));
        found.hide();
        dialog.show();
        settle();
        dialog.grab().save(QString("ui-preview/add-friend-%1.png").arg(mode == ElaThemeType::Light ? "light" : "dark"));
        dialog.hide();
    }
    QObject::disconnect(loginConnection);
    QObject::disconnect(stateConnection);
    tcp->slot_disconnect();
    qInfo() << "Workspace checks failed:" << failures;
    return failures ? 1 : 0;
}
