#pragma once
#include "ChatGraphics.h"
#include "ThemeManager.h"
#include <QDialog>
#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QScreen>
#include <QGuiApplication>
#include <QShowEvent>
#include <QHideEvent>
#include <QPointer>

// Shared, layout-driven profile card for search, invitation and acceptance.
class FriendDialog : public QDialog {
public:
    explicit FriendDialog(const QString& title, const QString& subtitle, QWidget* parent)
        : QDialog(parent) {
        setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setModal(true);
        setWindowTitle(title);
        setFixedWidth(480);
        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(20, 20, 20, 20);
        panel_ = new QWidget(this);
        panel_->setObjectName("friendCard");
        outer->addWidget(panel_);
        auto* shadow = new QGraphicsDropShadowEffect(panel_);
        shadow->setBlurRadius(32);
        shadow->setOffset(0, 8);
        shadow->setColor(QColor(0, 0, 0, 85));
        panel_->setGraphicsEffect(shadow);
        body = new QVBoxLayout(panel_);
        body->setContentsMargins(28, 24, 28, 24);
        body->setSpacing(16);
        auto* header = new QHBoxLayout;
        auto* heading = new QLabel(title, panel_);
        heading->setObjectName("dialogHeading");
        auto* close = new QPushButton(QString::fromUtf8("×"), panel_);
        close->setObjectName("dialogClose");
        close->setFixedSize(30, 30);
        close->setToolTip(tr("关闭"));
        header->addWidget(heading, 1);
        header->addWidget(close);
        body->addLayout(header);
        connect(close, &QPushButton::clicked, this, &QDialog::reject);
        auto* intro = new QLabel(subtitle, panel_);
        intro->setObjectName("dialogMuted");
        intro->setWordWrap(true);
        body->addWidget(intro);
        auto* profile = new QWidget(panel_);
        profile->setObjectName("profileCard");
        auto* row = new QHBoxLayout(profile);
        row->setContentsMargins(16, 16, 16, 16);
        row->setSpacing(14);
        avatar_ = new QLabel(profile);
        avatar_->setFixedSize(60, 60);
        avatar_->setAlignment(Qt::AlignCenter);
        avatar_->setObjectName("profileAvatar");
        row->addWidget(avatar_);
        auto* details = new QVBoxLayout;
        name_ = new QLabel(profile);
        name_->setTextFormat(Qt::PlainText);
        name_->setWordWrap(true);
        name_->setObjectName("profileName");
        uid_ = new QLabel(profile);
        uid_->setObjectName("dialogMuted");
        details->addWidget(name_);
        details->addWidget(uid_);
        row->addLayout(details, 1);
        body->addWidget(profile);
        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
                [this](ElaThemeType::ThemeMode) { applyPalette(); });
        applyPalette();
    }
    ~FriendDialog() override { delete shade_; }
protected:
    QVBoxLayout* body{};
    void setProfile(int uid, const QString& name, const QString& icon) {
        name_->setText(name);
        uid_->setText(tr("UID  %1").arg(uid));
        const QPixmap pixmap(icon);
        if (pixmap.isNull()) {
            avatar_->setText(name.left(1).toUpper());
        } else avatar_->setPixmap(roundAvatar(pixmap, 60));
    }
    QLabel* addHint(const QString& text, const char* objectName = "dialogMuted") {
        auto* label = new QLabel(text, this);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setObjectName(objectName);
        body->addWidget(label);
        return label;
    }
    QPushButton* addActions(const QString& text) {
        auto* row = new QHBoxLayout;
        auto* cancel = new QPushButton(tr("取消"), this);
        cancel->setObjectName("dialogSecondary");
        auto* primary = new QPushButton(text, this);
        primary->setObjectName("dialogPrimary");
        primary->setMinimumHeight(42);
        cancel->setMinimumHeight(42);
        row->addWidget(cancel);
        row->addWidget(primary, 1);
        body->addLayout(row);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        return primary;
    }
    void showEvent(QShowEvent* event) override {
        QDialog::showEvent(event);
        adjustSize();
        if (auto* parent = parentWidget()) {
            auto* owner = parent->window();
            if (!shade_) {
                shade_ = new QWidget(owner);
                shade_->setObjectName("friendDialogShade");
                shade_->setStyleSheet("background: rgba(9, 15, 26, 110);");
            }
            shade_->setGeometry(owner->rect());
            shade_->show(); shade_->raise();
            const auto center = owner->mapToGlobal(owner->rect().center());
            const auto bounds = owner->screen()->availableGeometry();
            move(qBound(bounds.left(), center.x() - width()/2, qMax(bounds.left(), bounds.right()-width()+1)),
                 qBound(bounds.top(), center.y() - height()/2, qMax(bounds.top(), bounds.bottom()-height()+1)));
        }
    }
    void hideEvent(QHideEvent* event) override {
        if (shade_) shade_->hide();
        QDialog::hideEvent(event);
    }
private:
    void applyPalette() {
        const bool dark = ThemeManager::instance().themeMode() == ElaThemeType::Dark;
        QString css = QStringLiteral(R"(
QDialog { background: transparent; }
QWidget#friendCard { background: @surface; border: 1px solid @border; border-radius: 22px; }
QLabel { color: @text; background: transparent; border: none; font: 14px 'Microsoft YaHei UI'; }
QLabel#dialogHeading { font-size: 22px; font-weight: 700; }
QLabel#dialogMuted { color: @muted; font-size: 13px; }
QLabel#profileName { font-size: 19px; font-weight: 600; }
QWidget#profileCard { background: @input; border: none; border-radius: 16px; }
QLabel#profileAvatar { background: #dce8ff; color: #0866ff; border-radius: 30px; font-size: 28px; }
QLineEdit, QTextEdit { background: @input; color: @text; border: 1px solid @border; border-radius: 10px; padding: 10px; font: 14px 'Microsoft YaHei UI'; }
QLineEdit:focus, QTextEdit:focus { border: 1px solid #3983ff; }
QPushButton { background: #0866ff; color: white; border: none; border-radius: 10px; padding: 8px 16px; font: 14px 'Microsoft YaHei UI'; }
QPushButton:hover { background: #2478ff; }
QPushButton:disabled { background: @input; color: @muted; }
QPushButton#dialogSecondary, QPushButton#dialogClose { background: @input; color: @text; }
QPushButton#dialogClose { padding: 0; font-size: 22px; border-radius: 15px; }
QLabel#operationStatus { color: @muted; font-size: 13px; }
)");
        css.replace("@surface", dark ? "#242832" : "#ffffff");
        css.replace("@input", dark ? "#303642" : "#f2f5fa");
        css.replace("@text", dark ? "#f1f4fa" : "#1a2538");
        css.replace("@muted", dark ? "#abb7cb" : "#748298");
        css.replace("@border", dark ? "#434b59" : "#e2e8f1");
        setStyleSheet(css);
    }
    QWidget* panel_{};
    QLabel* avatar_{};
    QLabel* name_{};
    QLabel* uid_{};
    QPointer<QWidget> shade_;
};
