#include "ChatWindow.h"

#include <QCoreApplication>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QIntValidator>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

#include "ChatDialog.h"
#include "ElaIconButton.h"
#include "ElaComboBox.h"
#include "ElaLineEdit.h"
#include "ElaMessageBar.h"
#include "ElaPushButton.h"
#include "ElaText.h"
#include "ThemeManager.h"
#include "ProxyManager.h"
#include "TcpMgr.h"
#include "usermgr.h"

ChatWindow::ChatWindow(QWidget* parent)
    : ElaWindow(parent)
{
    setWindowTitle(tr("Nebula Chat"));
    setWindowIcon(QIcon(":/new/prefix1/res/R-C.png"));
    resize(1280, 800);
    setMinimumSize(1000, 680);
    setIsNavigationBarEnable(false);
    setUserInfoCardVisible(false);
    setIsCentralStackedWidgetTransparent(true);

    auto* shell = new QWidget(this);
    shell->setObjectName("chatShell");
    auto* shellLayout = new QHBoxLayout(shell);
    shellLayout->setContentsMargins(10, 10, 10, 10);
    shellLayout->setSpacing(10);

    auto* rail = new QWidget(shell);
    rail->setObjectName("navigationRail");
    rail->setFixedWidth(64);
    auto* railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(8, 12, 8, 12);
    railLayout->setSpacing(10);

    messagesButton_ = new ElaIconButton(ElaIconType::Messages, 19, 48, 48, rail);
    contactsButton_ = new ElaIconButton(ElaIconType::AddressBook, 19, 48, 48, rail);
    settingsButton_ = new ElaIconButton(ElaIconType::GearComplex, 19, 48, 48, rail);
    messagesButton_->setToolTip(tr("Messages"));
    contactsButton_->setToolTip(tr("Contacts"));
    settingsButton_->setToolTip(tr("Settings"));
    railLayout->addWidget(messagesButton_);
    railLayout->addWidget(contactsButton_);
    railLayout->addStretch();
    railLayout->addWidget(settingsButton_);

    themeButton_ = new ElaIconButton(ElaIconType::MoonStars, 18, 48, 48, rail);
    themeButton_->setToolTip(tr("Switch theme"));
    railLayout->addWidget(themeButton_);

    auto* logoutButton = new ElaIconButton(ElaIconType::RightFromBracket, 18, 48, 48, rail);
    logoutButton->setToolTip(tr("Sign out"));
    railLayout->addWidget(logoutButton);

    contentStack_ = new QStackedWidget(shell);
    contentStack_->setObjectName("contentStack");
    workspace_ = new ChatDialog(contentStack_);
    workspace_->setExternalNavigationEnabled(true);
    workspace_->setMinimumSize(0, 0);
    workspace_->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    workspace_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QFile legacyStyle(QCoreApplication::applicationDirPath() + "/style/this_style.qss");
    if (legacyStyle.open(QFile::ReadOnly)) {
        workspace_->setStyleSheet(QString::fromUtf8(legacyStyle.readAll()));
    }

    contentStack_->addWidget(workspace_);
    contentStack_->addWidget(createSettingsPage(contentStack_));
    shellLayout->addWidget(rail);
    shellLayout->addWidget(contentStack_, 1);
    addCentralWidget(shell);

    const QString icon = UserMgr::Getinstance()->GetIcon();
    if (!icon.isEmpty()) {
        auto* avatar = new QLabel(rail);
        QPixmap pixmap(icon);
        avatar->setPixmap(pixmap.scaled(40, 40, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        avatar->setFixedSize(40, 40);
        avatar->setScaledContents(true);
        railLayout->insertWidget(0, avatar, 0, Qt::AlignHCenter);
    }

    connect(messagesButton_, &QPushButton::clicked, this, &ChatWindow::showMessages);
    connect(contactsButton_, &QPushButton::clicked, this, &ChatWindow::showContacts);
    connect(settingsButton_, &QPushButton::clicked, this, &ChatWindow::showSettings);
    connect(themeButton_, &QPushButton::clicked, &ThemeManager::instance(), &ThemeManager::toggleTheme);
    connect(logoutButton, &QPushButton::clicked, this, &ChatWindow::logoutRequested);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &ChatWindow::applyTheme);
    connect(TcpMgr::Getinstance().get(), &TcpMgr::sig_connection_state,
            this, [this](const QString& message, bool connected) {
        if (connected) {
            ElaMessageBar::success(ElaMessageBarType::TopRight, tr("Connection restored"), message, 2800, this);
        } else {
            ElaMessageBar::warning(ElaMessageBarType::TopRight, tr("Connection status"), message, 3200, this);
        }
    });

    showMessages();
    applyTheme(ThemeManager::instance().themeMode());
}

QWidget* ChatWindow::createSettingsPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    page->setObjectName("settingsPage");
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(48, 42, 48, 42);
    layout->setSpacing(14);

    auto* appearanceTitle = new ElaText(tr("Appearance"), page);
    appearanceTitle->setTextPixelSize(28);
    auto* appearanceDescription = new ElaText(
        tr("Choose the theme used across authentication, conversations and contacts."), page);
    appearanceDescription->setTextPixelSize(14);
    appearanceDescription->setWordWrap(true);
    auto* themeButton = new ElaPushButton(tr("Switch light / dark theme"), page);
    themeButton->setMaximumWidth(280);
    connect(themeButton, &QPushButton::clicked,
            &ThemeManager::instance(), &ThemeManager::toggleTheme);

    auto* networkTitle = new ElaText(tr("Network route"), page);
    networkTitle->setTextPixelSize(24);
    auto* networkDescription = new ElaText(
        tr("Keep Direct for the normal entry. Select SOCKS5 backup when v2rayN or Mihomo is running locally."), page);
    networkDescription->setTextPixelSize(14);
    networkDescription->setWordWrap(true);

    proxyMode_ = new ElaComboBox(page);
    proxyMode_->addItem(tr("Direct (normal entry)"), static_cast<int>(ProxyManager::Mode::Direct));
    proxyMode_->addItem(tr("System proxy"), static_cast<int>(ProxyManager::Mode::System));
    proxyMode_->addItem(tr("SOCKS5 (Xray backup)"), static_cast<int>(ProxyManager::Mode::Socks5));
    proxyMode_->setMaximumWidth(360);

    proxyHost_ = new ElaLineEdit(page);
    proxyHost_->setPlaceholderText(tr("SOCKS5 host, for example 127.0.0.1"));
    proxyHost_->setMaximumWidth(360);
    proxyPort_ = new ElaLineEdit(page);
    proxyPort_->setPlaceholderText(tr("SOCKS5 port, for example 10808"));
    proxyPort_->setValidator(new QIntValidator(1, 65535, proxyPort_));
    proxyPort_->setMaximumWidth(360);

    auto* applyButton = new ElaPushButton(tr("Apply network route"), page);
    applyButton->setMaximumWidth(280);
    connect(proxyMode_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateProxyInputs(); });
    connect(applyButton, &QPushButton::clicked, this, &ChatWindow::applyProxySettings);

    const auto& proxy = ProxyManager::instance();
    const int modeIndex = proxyMode_->findData(static_cast<int>(proxy.mode()));
    proxyMode_->setCurrentIndex(modeIndex < 0 ? 0 : modeIndex);
    proxyHost_->setText(proxy.host());
    proxyPort_->setText(QString::number(proxy.port()));
    updateProxyInputs();

    layout->addWidget(appearanceTitle);
    layout->addWidget(appearanceDescription);
    layout->addWidget(themeButton);
    layout->addSpacing(24);
    layout->addWidget(networkTitle);
    layout->addWidget(networkDescription);
    layout->addWidget(proxyMode_);
    layout->addWidget(proxyHost_);
    layout->addWidget(proxyPort_);
    layout->addWidget(applyButton);
    layout->addStretch();
    return page;
}

void ChatWindow::applyProxySettings()
{
    const auto mode = static_cast<ProxyManager::Mode>(proxyMode_->currentData().toInt());
    QString error;
    if (!ProxyManager::instance().apply(mode, proxyHost_->text(),
                                        static_cast<quint16>(proxyPort_->text().toUInt()), &error)) {
        ElaMessageBar::error(ElaMessageBarType::TopRight, tr("Invalid proxy"), error, 3200, this);
        return;
    }
    TcpMgr::Getinstance()->slot_reconnect_for_proxy();
    ElaMessageBar::success(ElaMessageBarType::TopRight, tr("Network route applied"),
                           tr("Current mode: %1").arg(ProxyManager::instance().modeName()), 2800, this);
}

void ChatWindow::updateProxyInputs()
{
    const auto mode = static_cast<ProxyManager::Mode>(proxyMode_->currentData().toInt());
    const bool socks = mode == ProxyManager::Mode::Socks5;
    proxyHost_->setEnabled(socks);
    proxyPort_->setEnabled(socks);
}

void ChatWindow::showMessages()
{
    contentStack_->setCurrentWidget(workspace_);
    workspace_->slot_side_chat();
    selectRailButton(messagesButton_);
}

void ChatWindow::showContacts()
{
    contentStack_->setCurrentWidget(workspace_);
    workspace_->slot_side_contact();
    selectRailButton(contactsButton_);
}

void ChatWindow::showSettings()
{
    contentStack_->setCurrentIndex(1);
    selectRailButton(settingsButton_);
}

void ChatWindow::selectRailButton(ElaIconButton* selected)
{
    messagesButton_->setIsSelected(selected == messagesButton_);
    contactsButton_->setIsSelected(selected == contactsButton_);
    settingsButton_->setIsSelected(selected == settingsButton_);
}

void ChatWindow::applyTheme(ElaThemeType::ThemeMode mode)
{
    const bool dark = mode == ElaThemeType::Dark;
    themeButton_->setAwesome(dark ? ElaIconType::SunBright : ElaIconType::MoonStars);
    themeButton_->setToolTip(dark ? tr("Switch to light theme") : tr("Switch to dark theme"));
    setStyleSheet(QString(
        "#navigationRail { background: %1; border: 1px solid %2; border-radius: 16px; }"
        "#contentStack, #settingsPage { background: %3; border: 1px solid %2; border-radius: 16px; }")
        .arg(dark ? "rgba(25,27,38,225)" : "rgba(252,252,255,235)",
             dark ? "#3B3F50" : "#D8DAE4",
             dark ? "rgba(30,32,43,220)" : "rgba(255,255,255,235)"));
    const auto widgets = findChildren<QWidget*>();
    for (QWidget* widget : widgets) {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }
    emit themeChanged(mode);
}
