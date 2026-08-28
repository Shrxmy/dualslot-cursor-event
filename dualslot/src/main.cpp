#include "MainWindow.h"
#include "Types.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QStyleFactory>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

void writeLogLine(const QString& line)
{
    static QMutex mutex;
    QMutexLocker lock(&mutex);
    QFile file(QCoreApplication::applicationDirPath() + QStringLiteral("/dualslot.log"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8());
        file.write(" ");
        file.write(line.toUtf8());
        file.write("\n");
    }
}

void qtMessageLogger(QtMsgType type, const QMessageLogContext&, const QString& message)
{
    static const char* levels[] = {"debug", "warning", "critical", "fatal", "info"};
    const int index = qBound(0, static_cast<int>(type), 4);
    writeLogLine(QStringLiteral("[Qt:%1] %2").arg(QString::fromLatin1(levels[index]), message));
}

#ifdef Q_OS_WIN
LONG WINAPI crashLogger(EXCEPTION_POINTERS* exception)
{
    const auto code = exception && exception->ExceptionRecord ? exception->ExceptionRecord->ExceptionCode : 0;
    const auto address = exception && exception->ExceptionRecord ? exception->ExceptionRecord->ExceptionAddress : nullptr;
    writeLogLine(QStringLiteral("[crash] Windows exception 0x%1 at 0x%2")
        .arg(code, 8, 16, QLatin1Char('0'))
        .arg(reinterpret_cast<quintptr>(address), 0, 16));
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    qInstallMessageHandler(qtMessageLogger);
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(crashLogger);
#endif
    writeLogLine(QStringLiteral("[app] DualSlot %1 started").arg(QStringLiteral(DUALSLOT_VERSION)));
    QCoreApplication::setOrganizationName(QStringLiteral("DualSlot"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("dualslot.local"));
    QCoreApplication::setApplicationName(QStringLiteral("DualSlot DS"));
    QCoreApplication::setApplicationVersion(QStringLiteral(DUALSLOT_VERSION));
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    qRegisterMetaType<dualslot::SlotState>();
    qRegisterMetaType<dualslot::FramePacket>();
    qRegisterMetaType<dualslot::ActiveCore>();

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("One DS shell with melonDS Slot-1 and mGBA Slot-2 playback"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("rom"), QStringLiteral("ROM(s) to attach; DS cards go to Slot-1, GB/GBA carts to Slot-2"), QStringLiteral("[rom…]"));
    parser.process(app);

    dualslot::MainWindow window;
    window.show();
    for (const QString& path : parser.positionalArguments())
        window.attachRom(path);
    return app.exec();
}
