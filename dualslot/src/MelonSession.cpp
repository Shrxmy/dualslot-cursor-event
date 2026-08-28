#include "MelonSession.h"

#include "FirmwareStore.h"

#include "Args.h"
#include "DSi.h"
#include "DSi_NAND.h"
#include "GBACart.h"
#include "GPU.h"
#include "NDS.h"
#include "NDSCart.h"
#include "Platform.h"
#include "SPI_Firmware.h"
#include "Savestate.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <array>
#include <cstring>
#include <optional>
#include <tuple>

namespace dualslot {
namespace {

QByteArray readFile(const QString& path, QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Cannot read %1: %2").arg(path, file.errorString());
        return {};
    }
    return file.readAll();
}

template<typename Array>
bool loadArray(const QString& path, std::unique_ptr<Array>& destination, QString& error)
{
    const QByteArray bytes = readFile(path, error);
    if (bytes.size() != static_cast<qsizetype>(std::tuple_size_v<Array>)) {
        error = QStringLiteral("%1 has size %2; expected %3 bytes.")
                    .arg(path).arg(bytes.size()).arg(std::tuple_size_v<Array>);
        return false;
    }
    destination = std::make_unique<Array>();
    std::memcpy(destination->data(), bytes.constData(), bytes.size());
    return true;
}

std::unique_ptr<melonDS::u8[]> ownedBytes(const QByteArray& bytes)
{
    auto result = std::make_unique<melonDS::u8[]>(static_cast<size_t>(bytes.size()));
    if (!bytes.isEmpty())
        std::memcpy(result.get(), bytes.constData(), static_cast<size_t>(bytes.size()));
    return result;
}

} // namespace

MelonSession::MelonSession(const FirmwareStore& firmware)
    : m_firmware(firmware)
{
}

MelonSession::~MelonSession()
{
    stop();
}

bool MelonSession::start(const SlotState& state, QString& error)
{
    stop();
    m_slots = state;
    m_title = QFileInfo(state.slot1).completeBaseName();
    m_ndsSavePath = state.slot1 + QStringLiteral(".sav");
    m_gbaSavePath = state.slot2.isEmpty() ? QString() : state.slot2 + QStringLiteral(".sav");
    m_gbaModeRequested = false;

    melonDS::NDSArgs ndsArgs;
    if (m_firmware.hasDsFirmware()) {
        if (!loadArray(m_firmware.path(QStringLiteral("bios9")), ndsArgs.ARM9BIOS, error) ||
            !loadArray(m_firmware.path(QStringLiteral("bios7")), ndsArgs.ARM7BIOS, error))
            return false;
        const QByteArray firmware = readFile(m_firmware.path(QStringLiteral("firmware")), error);
        if (firmware.isEmpty())
            return false;
        ndsArgs.Firmware = melonDS::Firmware(reinterpret_cast<const melonDS::u8*>(firmware.constData()), static_cast<melonDS::u32>(firmware.size()));
        m_firmwareWritePath = m_firmware.path(QStringLiteral("firmware"));
    }
    ndsArgs.OutputSampleRate = 48000.0;
#ifdef JIT_ENABLED
    // melonDS JIT currently crashes on this MinGW/GCC build in DSi direct boot.
    // The interpreter is slower but stable and is the correct v1 default.
    ndsArgs.JIT = std::nullopt;
#endif

    // DSi direct boot is not yet stable in this thin frontend. DSi-enhanced
    // retail games also contain an NTR executable and are fully playable on a
    // regular DS, so use compatibility mode instead of risking a host crash.
    m_usingDsi = false;
    if (state.slot1Type == RomType::Dsi)
        melonDS::Platform::Log(melonDS::Platform::LogLevel::Info,
            "DSi-enhanced title detected; using stable Nintendo DS compatibility mode\n");
    if (m_usingDsi) {
        melonDS::DSiArgs dsiArgs;
        static_cast<melonDS::NDSArgs&>(dsiArgs) = std::move(ndsArgs);
        if (!loadArray(m_firmware.path(QStringLiteral("bios9i")), dsiArgs.ARM9iBIOS, error) ||
            !loadArray(m_firmware.path(QStringLiteral("bios7i")), dsiArgs.ARM7iBIOS, error))
            return false;
        const QByteArray dsiFirmware = readFile(m_firmware.path(QStringLiteral("dsiFirmware")), error);
        if (dsiFirmware.isEmpty())
            return false;
        dsiArgs.Firmware = melonDS::Firmware(reinterpret_cast<const melonDS::u8*>(dsiFirmware.constData()), static_cast<melonDS::u32>(dsiFirmware.size()));
        m_firmwareWritePath = m_firmware.path(QStringLiteral("dsiFirmware"));

        auto* nandFile = melonDS::Platform::OpenFile(m_firmware.path(QStringLiteral("nand")).toStdString(), melonDS::Platform::ReadWriteExisting);
        if (!nandFile) {
            error = QStringLiteral("Cannot open DSi NAND for reading and writing.");
            return false;
        }
        dsiArgs.NANDImage.emplace(nandFile, &(*dsiArgs.ARM7iBIOS)[0x8308]);
        if (!*dsiArgs.NANDImage) {
            error = QStringLiteral("The DSi NAND could not be decrypted with bios7i.");
            return false;
        }
        m_nds = std::make_unique<melonDS::DSi>(std::move(dsiArgs), this);
    } else {
        m_nds = std::make_unique<melonDS::NDS>(std::move(ndsArgs), this);
    }

    const QByteArray rom = readFile(state.slot1, error);
    if (rom.isEmpty()) {
        stop();
        return false;
    }
    QByteArray save;
    if (QFile::exists(m_ndsSavePath))
        save = readFile(m_ndsSavePath, error);
    melonDS::NDSCart::NDSCartArgs cartArgs;
    cartArgs.SRAM = ownedBytes(save);
    cartArgs.SRAMLength = static_cast<melonDS::u32>(save.size());
    auto cart = melonDS::NDSCart::ParseROM(ownedBytes(rom), static_cast<melonDS::u32>(rom.size()), this, std::move(cartArgs));
    if (!cart) {
        error = QStringLiteral("melonDS rejected the Slot-1 ROM.");
        stop();
        return false;
    }
    m_nds->SetNDSCart(std::move(cart));
    if (!loadSlot2(state, error)) {
        stop();
        return false;
    }

    m_nds->Reset();
    m_nds->SetupDirectBoot(QFileInfo(state.slot1).fileName().toStdString());
    m_nds->Start();
    return true;
}

bool MelonSession::loadSlot2(const SlotState& state, QString& error)
{
    if (m_usingDsi || state.slot2.isEmpty() || state.slot2Type != RomType::Gba)
        return true;
    const QByteArray rom = readFile(state.slot2, error);
    if (rom.isEmpty())
        return false;
    QByteArray save;
    if (QFile::exists(m_gbaSavePath))
        save = readFile(m_gbaSavePath, error);
    auto cart = melonDS::GBACart::ParseROM(ownedBytes(rom), static_cast<melonDS::u32>(rom.size()),
                                           ownedBytes(save), static_cast<melonDS::u32>(save.size()), this);
    if (!cart) {
        error = QStringLiteral("melonDS rejected the GBA Slot-2 cartridge.");
        return false;
    }
    m_nds->SetGBACart(std::move(cart));
    return true;
}

bool MelonSession::updateSlots(const SlotState& state, QString& error)
{
    if (!m_nds || state.slot1 != m_slots.slot1)
        return false;
    if (state.slot2 == m_slots.slot2)
        return true;
    if (!m_gbaSavePath.isEmpty() &&
        !writeBytes(m_gbaSavePath, m_nds->GetGBASave(), m_nds->GetGBASaveLength(), error))
        return false;
    m_nds->EjectGBACart();
    m_slots = state;
    m_gbaSavePath = state.slot2.isEmpty() ? QString() : state.slot2 + QStringLiteral(".sav");
    return loadSlot2(state, error);
}

void MelonSession::stop()
{
    if (!m_nds)
        return;
    QString ignored;
    flushSave(ignored);
    m_nds->Stop();
    m_nds.reset();
}

FramePacket MelonSession::frame()
{
    FramePacket packet;
    packet.core = ActiveCore::Melon;
    packet.detail = m_usingDsi ? QStringLiteral("Nintendo DSi · %1").arg(m_title)
        : (m_slots.slot1Type == RomType::Dsi
            ? QStringLiteral("Nintendo DS compatibility mode · %1").arg(m_title)
            : QStringLiteral("Nintendo DS · %1").arg(m_title));
    if (!m_nds || !m_nds->IsRunning())
        return packet;

    m_nds->SetKeyMask((~m_keys) & 0xFFFu);
    if (m_touch.down)
        m_nds->TouchScreen(static_cast<melonDS::u16>(m_touch.x), static_cast<melonDS::u16>(m_touch.y));
    else
        m_nds->ReleaseScreen();
    m_nds->RunFrame();

    void* top = nullptr;
    void* bottom = nullptr;
    if (m_nds->GPU.GetFramebuffers(&top, &bottom) && top && bottom) {
        constexpr qsizetype stride = 256 * sizeof(std::uint32_t);
        packet.top = QImage(static_cast<const uchar*>(top), 256, 192, stride, QImage::Format_ARGB32).copy();
        packet.bottom = QImage(static_cast<const uchar*>(bottom), 256, 192, stride, QImage::Format_ARGB32).copy();
    }

    const int samples = m_nds->SPU.GetOutputSize();
    if (samples > 0) {
        packet.audio.resize(static_cast<qsizetype>(samples * 2 * sizeof(std::int16_t)));
        const int read = m_nds->SPU.ReadOutput(reinterpret_cast<melonDS::s16*>(packet.audio.data()), samples);
        packet.audio.resize(static_cast<qsizetype>(read * 2 * sizeof(std::int16_t)));
    }
    return packet;
}

void MelonSession::setKeys(std::uint32_t pressed) { m_keys = pressed; }
void MelonSession::setTouch(const TouchPoint& touch) { m_touch = touch; }

bool MelonSession::writeBytes(const QString& path, const std::uint8_t* data, std::uint32_t length, QString& error)
{
    if (path.isEmpty() || !data || !length)
        return true;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(reinterpret_cast<const char*>(data), length) != length || !file.commit()) {
        error = QStringLiteral("Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

bool MelonSession::flushSave(QString& error)
{
    if (!m_nds)
        return true;
    bool ok = writeBytes(m_ndsSavePath, m_nds->GetNDSSave(), m_nds->GetNDSSaveLength(), error);
    if (ok && !m_gbaSavePath.isEmpty())
        ok = writeBytes(m_gbaSavePath, m_nds->GetGBASave(), m_nds->GetGBASaveLength(), error);
    if (ok && m_firmwareDirty && !m_firmwareWritePath.isEmpty())
        ok = writeBytes(m_firmwareWritePath, m_nds->GetFirmware().Buffer(), m_nds->GetFirmware().Length(), error);
    if (ok)
        m_ndsSaveDirty = m_gbaSaveDirty = m_firmwareDirty = false;
    return ok;
}

bool MelonSession::saveState(const QString& path, QString& error)
{
    if (!m_nds)
        return false;
    melonDS::Savestate state;
    if (!m_nds->DoSavestate(&state) || state.Error) {
        error = QStringLiteral("melonDS could not serialize this state.");
        return false;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    return writeBytes(path, static_cast<const std::uint8_t*>(state.Buffer()), state.Length(), error);
}

bool MelonSession::loadState(const QString& path, QString& error)
{
    if (!m_nds)
        return false;
    const QByteArray bytes = readFile(path, error);
    if (bytes.isEmpty())
        return false;
    melonDS::Savestate state(const_cast<char*>(bytes.constData()), static_cast<melonDS::u32>(bytes.size()), false);
    if (!m_nds->DoSavestate(&state) || state.Error) {
        error = QStringLiteral("This melonDS state is invalid or belongs to another console configuration.");
        return false;
    }
    return true;
}

void MelonSession::reset()
{
    if (!m_nds)
        return;
    m_nds->Reset();
    m_nds->SetupDirectBoot(QFileInfo(m_slots.slot1).fileName().toStdString());
    m_nds->Start();
}

void MelonSession::melonStopped(int reason)
{
    m_gbaModeRequested = reason == static_cast<int>(melonDS::Platform::StopReason::GBAModeNotSupported);
}

} // namespace dualslot
