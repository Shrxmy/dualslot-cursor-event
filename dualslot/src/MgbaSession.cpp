#include "MgbaSession.h"

#include "FirmwareStore.h"

extern "C" {
#include <mgba/core/core.h>
#include <mgba/core/serialize.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/vfs.h>
}

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstdlib>
#include <fcntl.h>

namespace dualslot {

MgbaSession::MgbaSession(const FirmwareStore& firmware)
    : m_firmware(firmware)
{
}

MgbaSession::~MgbaSession()
{
    stop();
}

bool MgbaSession::start(const SlotState& slots, QString& error)
{
    stop();
    m_romPath = slots.slot2;
    m_savePath = m_romPath + QStringLiteral(".sav");
    m_title = QFileInfo(m_romPath).completeBaseName();
    const QByteArray romName = QFile::encodeName(m_romPath);

    m_core = mCoreFind(romName.constData());
    if (!m_core) {
        error = QStringLiteral("mGBA could not identify %1.").arg(QFileInfo(m_romPath).fileName());
        return false;
    }
    if (!m_core->init(m_core)) {
        error = QStringLiteral("mGBA core initialization failed.");
        m_core = nullptr;
        return false;
    }

    mCoreInitConfig(m_core, "dualslot");
    m_configInitialized = true;
    mCoreConfigSetDefaultValue(&m_core->config, "sampleRate", "48000");
    mCoreConfigSetDefaultValue(&m_core->config, "audioBuffers", "1024");
    mCoreConfigSetDefaultValue(&m_core->config, "idleOptimization", "detect");
    mCoreLoadConfig(m_core);

    m_core->baseVideoSize(m_core, &m_width, &m_height);
    m_video.resize(static_cast<qsizetype>(m_width * m_height));
    m_core->setVideoBuffer(m_core, reinterpret_cast<mColor*>(m_video.data()), m_width);
    m_core->setAudioBufferSize(m_core, 4096);

    const QString biosPath = m_core->platform(m_core) == mPLATFORM_GBA
        ? m_firmware.path(QStringLiteral("gbaBios"))
        : m_firmware.path(slots.slot2Type == RomType::Gbc ? QStringLiteral("gbcBios") : QStringLiteral("gbBios"));
    if (!biosPath.isEmpty()) {
        const QByteArray encoded = QFile::encodeName(biosPath);
        if (VFile* bios = VFileOpen(encoded.constData(), O_RDONLY)) {
            if (!m_core->loadBIOS(m_core, bios, 0))
                bios->close(bios);
        }
    }

    if (!mCoreLoadFile(m_core, romName.constData())) {
        error = QStringLiteral("mGBA failed to load %1.").arg(QFileInfo(m_romPath).fileName());
        stop();
        return false;
    }

    const QByteArray saveName = QFile::encodeName(m_savePath);
    if (!mCoreLoadSaveFile(m_core, saveName.constData(), false)) {
        error = QStringLiteral("Could not open save file %1.").arg(m_savePath);
        stop();
        return false;
    }

    m_core->reset(m_core);
    return true;
}

void MgbaSession::stop()
{
    if (!m_core)
        return;
    QString ignored;
    flushSave(ignored);
    if (m_configInitialized)
        mCoreConfigDeinit(&m_core->config);
    m_core->deinit(m_core);
    m_core = nullptr;
    m_configInitialized = false;
    m_video.clear();
}

FramePacket MgbaSession::frame()
{
    FramePacket packet;
    packet.core = ActiveCore::Mgba;
    packet.detail = m_title;
    if (!m_core)
        return packet;

    m_core->setKeys(m_core, m_keys & 0x3FFu);
    m_core->runFrame(m_core);
    m_core->currentVideoSize(m_core, &m_width, &m_height);
    packet.top = QImage(reinterpret_cast<const uchar*>(m_video.constData()), static_cast<int>(m_width),
                        static_cast<int>(m_height), static_cast<qsizetype>(m_width * sizeof(std::uint32_t)),
                        QImage::Format_RGBA8888).copy();

    if (mAudioBuffer* audio = m_core->getAudioBuffer(m_core)) {
        const size_t frames = mAudioBufferAvailable(audio);
        if (frames) {
            packet.audio.resize(static_cast<qsizetype>(frames * 2 * sizeof(std::int16_t)));
            const size_t read = mAudioBufferRead(audio, reinterpret_cast<std::int16_t*>(packet.audio.data()), frames);
            packet.audio.resize(static_cast<qsizetype>(read * 2 * sizeof(std::int16_t)));
        }
    }
    return packet;
}

void MgbaSession::setKeys(std::uint32_t pressed)
{
    m_keys = pressed;
}

bool MgbaSession::flushSave(QString& error)
{
    if (!m_core)
        return true;
    void* data = nullptr;
    const size_t size = m_core->savedataClone(m_core, &data);
    if (!size)
        return true;

    // The core keeps its save VFile open. Write in-place rather than atomically
    // replacing the path (which is not permitted for an open file on Windows).
    QFile save(m_savePath);
    if (!save.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        save.write(static_cast<const char*>(data), static_cast<qint64>(size)) != static_cast<qint64>(size) || !save.flush()) {
        error = QStringLiteral("Failed to flush GBA save %1: %2").arg(m_savePath, save.errorString());
        std::free(data);
        return false;
    }
    save.close();
    std::free(data);
    return true;
}

bool MgbaSession::saveState(const QString& path, QString& error)
{
    if (!m_core)
        return false;
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QByteArray encoded = QFile::encodeName(path);
    VFile* file = VFileOpen(encoded.constData(), O_CREAT | O_TRUNC | O_RDWR);
    if (!file) {
        error = QStringLiteral("Cannot create savestate %1.").arg(path);
        return false;
    }
    const bool ok = mCoreSaveStateNamed(m_core, file, SAVESTATE_ALL);
    file->close(file);
    if (!ok)
        error = QStringLiteral("mGBA rejected the savestate.");
    return ok;
}

bool MgbaSession::loadState(const QString& path, QString& error)
{
    if (!m_core)
        return false;
    const QByteArray encoded = QFile::encodeName(path);
    VFile* file = VFileOpen(encoded.constData(), O_RDONLY);
    if (!file) {
        error = QStringLiteral("Cannot open savestate %1.").arg(path);
        return false;
    }
    const bool ok = mCoreLoadStateNamed(m_core, file, SAVESTATE_ALL);
    file->close(file);
    if (!ok)
        error = QStringLiteral("This mGBA savestate is invalid or belongs to another game.");
    return ok;
}

void MgbaSession::reset()
{
    if (m_core)
        m_core->reset(m_core);
}

} // namespace dualslot
