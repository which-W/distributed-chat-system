#pragma once

#include "ElaWidget.h"

class LoginDialog;
class RegisterDialog;
class ResetDialog;
class ElaIconButton;
class QStackedWidget;

class AuthWindow final : public ElaWidget
{
    Q_OBJECT

public:
    explicit AuthWindow(QWidget* parent = nullptr);

signals:
    void authenticationSucceeded();
    void themeChanged(ElaThemeType::ThemeMode mode);

private:
    void showLogin();
    void showRegister();
    void showReset();
    void applyTheme(ElaThemeType::ThemeMode mode);

    QStackedWidget* pages_{nullptr};
    ElaIconButton* themeButton_{nullptr};
    LoginDialog* login_{nullptr};
    RegisterDialog* register_{nullptr};
    ResetDialog* reset_{nullptr};
};
