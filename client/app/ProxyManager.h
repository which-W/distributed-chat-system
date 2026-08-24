#pragma once

#include <QObject>
#include <QNetworkProxy>
#include <QString>

class ProxyManager final : public QObject
{
    Q_OBJECT

public:
    enum class Mode {
        Direct,
        System,
        Socks5
    };
    Q_ENUM(Mode)

    static ProxyManager& instance();

    void initialize(const QString& configPath);
    Mode mode() const;
    QString host() const;
    quint16 port() const;
    QNetworkProxy socketProxy() const;
    QString modeName() const;

    bool apply(Mode mode, const QString& host, quint16 port, QString* error = nullptr);
    static Mode modeFromString(const QString& value);
    static QString modeToString(Mode mode);

signals:
    void proxyChanged();

private:
    explicit ProxyManager(QObject* parent = nullptr);
    void applyToApplication();

    Mode mode_{Mode::Direct};
    QString host_{QStringLiteral("127.0.0.1")};
    quint16 port_{10808};
    bool initialized_{false};
};
