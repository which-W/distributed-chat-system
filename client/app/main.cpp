#include "MainWindow.h"
#include <QtWidgets/QApplication>
#include <qfile.h>
#include <qdebug.h>
#include <QUrl>
#include "global.h"
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QFile qss("./style/this_style.qss");
    if (qss.open(QFile::ReadOnly)) {
        QString style = QLatin1String(qss.readAll());
        a.setStyleSheet(style);
        qss.close();

    }
    else {
        qDebug("open qss failed");
    }

    QString fileName = "config.ini";
    QString app_path = QCoreApplication::applicationDirPath();
    QString config_path = QDir::toNativeSeparators(app_path + QDir::separator() + fileName);
    QSettings settings(config_path, QSettings::IniFormat);
    allow_insecure_transport = settings.value("Security/AllowInsecure", false).toBool();
    gate_url_prefix = settings.value("GateServer/BaseUrl").toString().trimmed();
    if (gate_url_prefix.isEmpty()) {
        const QString gateHost = settings.value("GateServer/host").toString().trimmed();
        const QString gatePort = settings.value("GateServer/port").toString().trimmed();
        gate_url_prefix = "http://" + gateHost + ":" + gatePort;
    }
    while (gate_url_prefix.endsWith('/')) {
        gate_url_prefix.chop(1);
    }

    const QUrl gateUrl(gate_url_prefix);
    if (!gateUrl.isValid() || gateUrl.host().isEmpty() ||
        (gateUrl.scheme() != "https" && !allow_insecure_transport)) {
        qCritical() << "Gate URL is invalid or insecure HTTP is disabled:" << gate_url_prefix;
        return EXIT_FAILURE;
    }

    MainWindow w;
    w.show();
    return a.exec();
}
