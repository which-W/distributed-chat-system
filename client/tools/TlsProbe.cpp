#include <QCoreApplication>
#include <QSslCipher>
#include <QSslSocket>
#include <QTextStream>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    out << "Qt: " << QT_VERSION_STR << '\n';
    out << "SSL 构建版本: " << QSslSocket::sslLibraryBuildVersionString() << '\n';
    out << "SSL 运行时版本: " << QSslSocket::sslLibraryVersionString() << '\n';
    out << "TLS 可用: " << (QSslSocket::supportsSsl() ? "是" : "否") << '\n';
    out.flush();
    if (!QSslSocket::supportsSsl()) {
        return EXIT_FAILURE;
    }

    const QStringList args = app.arguments();
    if (args.size() < 2) {
        return EXIT_SUCCESS;
    }

    const QString host = args.at(1);
    bool portOk = true;
    const quint16 port = args.size() >= 3 ? args.at(2).toUShort(&portOk) : 443;
    if (!portOk || port == 0) {
        out << "端口无效\n";
        return EXIT_FAILURE;
    }

    QSslSocket socket;
    socket.setPeerVerifyMode(QSslSocket::VerifyPeer);
    socket.connectToHostEncrypted(host, port);
    if (!socket.waitForEncrypted(10000)) {
        out << "TLS 握手失败: " << socket.errorString() << '\n';
        out.flush();
        return EXIT_FAILURE;
    }

    out << "已验证对端: " << host << ':' << port << '\n';
    out << "密码套件: " << socket.sessionCipher().name() << '\n';
    out.flush();
    socket.disconnectFromHost();
    return EXIT_SUCCESS;
}
