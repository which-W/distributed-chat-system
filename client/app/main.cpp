#include "MainWindow.h"
#include <QtWidgets/QApplication>
#include <qfile.h>
#include <qdebug.h>
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
    QString Gate_Host = settings.value("GateServer/host").toString();
    QString Gate_Port = settings.value("GateServer/port").toString();
    gate_url_prefix = "http://" + Gate_Host + ":" + Gate_Port;

    MainWindow w;
    w.show();
    return a.exec();
}
