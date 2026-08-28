#include "MainWindow.h"
#include "Types.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QStyleFactory>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
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
