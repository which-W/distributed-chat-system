#pragma once

#include <QObject>

#include "ElaWidgetToolsDef.h"

class ThemeManager final : public QObject
{
    Q_OBJECT

public:
    static ThemeManager& instance();

    void initialize();
    ElaThemeType::ThemeMode themeMode() const;

public slots:
    void setThemeMode(ElaThemeType::ThemeMode mode);
    void toggleTheme();

signals:
    void themeChanged(ElaThemeType::ThemeMode mode);

private:
    explicit ThemeManager(QObject* parent = nullptr);
};
