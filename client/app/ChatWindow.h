#pragma once

#include "ElaWindow.h"

class ChatDialog;
class ElaIconButton;
class QStackedWidget;

class ChatWindow final : public ElaWindow
{
    Q_OBJECT

public:
    explicit ChatWindow(QWidget* parent = nullptr);

signals:
    void logoutRequested();
    void themeChanged(ElaThemeType::ThemeMode mode);

private:
    void showMessages();
    void showContacts();
    void showSettings();
    void selectRailButton(ElaIconButton* selected);
    void applyTheme(ElaThemeType::ThemeMode mode);

    ChatDialog* workspace_{nullptr};
    QStackedWidget* contentStack_{nullptr};
    ElaIconButton* messagesButton_{nullptr};
    ElaIconButton* contactsButton_{nullptr};
    ElaIconButton* settingsButton_{nullptr};
    ElaIconButton* themeButton_{nullptr};
};
