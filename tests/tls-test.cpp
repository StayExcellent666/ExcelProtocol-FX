#include <QCoreApplication>
#include <QDebug>
#include <QSslSocket>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2) {
        qCritical() << "Expected the Qt plugins directory";
        return 2;
    }
    QCoreApplication::addLibraryPath(QString::fromLocal8Bit(argv[1]));
    if (!QSslSocket::supportsSsl()) {
        qCritical() << "Qt TLS initialization failed" << QSslSocket::sslLibraryBuildVersionString();
        return 1;
    }
    qInfo() << "Qt TLS ready:" << QSslSocket::activeBackend()
            << QSslSocket::sslLibraryVersionString();
    return 0;
}
