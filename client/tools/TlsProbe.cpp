#include <QCoreApplication>
#include <QSslCipher>
#include <QSslSocket>
#include <QTextStream>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    out << "Qt: " << QT_VERSION_STR << '\n';
    out << "SSL build version: " << QSslSocket::sslLibraryBuildVersionString() << '\n';
    out << "SSL runtime version: " << QSslSocket::sslLibraryVersionString() << '\n';
    out << "TLS available: " << (QSslSocket::supportsSsl() ? "yes" : "no") << '\n';
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
        out << "Invalid port\n";
        return EXIT_FAILURE;
    }

    QSslSocket socket;
    socket.setPeerVerifyMode(QSslSocket::VerifyPeer);
    socket.connectToHostEncrypted(host, port);
    if (!socket.waitForEncrypted(10000)) {
        out << "TLS handshake failed: " << socket.errorString() << '\n';
        out.flush();
        return EXIT_FAILURE;
    }

    out << "Verified peer: " << host << ':' << port << '\n';
    out << "Cipher: " << socket.sessionCipher().name() << '\n';
    out.flush();
    socket.disconnectFromHost();
    return EXIT_SUCCESS;
}
