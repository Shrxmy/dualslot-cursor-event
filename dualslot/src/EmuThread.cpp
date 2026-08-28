#include "EmuThread.h"

#include "FirmwareStore.h"
#include "IEmuSession.h"
#include "MelonSession.h"
#include "MgbaSession.h"

#include <SDL.h>

#include <QElapsedTimer>

namespace dualslot {

EmuThread::EmuThread(const FirmwareStore& firmware, QObject* parent)
    : QThread(parent), m_firmware(firmware)
{
}

EmuThread::~EmuThread()
{
    shutdown();
}

void EmuThread::requestSlots(const SlotState& state)
{
    std::lock_guard lock(m_mutex);
    m_pendingSlots = state;
}

void EmuThread::setTouch(const TouchPoint& touch)
{
    std::lock_guard lock(m_mutex);
    m_touch = touch;
}

void EmuThread::requestReset()
{
    std::lock_guard lock(m_mutex);
    m_command = Command{CommandType::Reset, {}};
}

void EmuThread::requestSaveState(const QString& path)
{
    std::lock_guard lock(m_mutex);
    m_command = Command{CommandType::SaveState, path};
}

void EmuThread::requestLoadState(const QString& path)
{
    std::lock_guard lock(m_mutex);
    m_command = Command{CommandType::LoadState, path};
}

void EmuThread::shutdown()
{
    m_stopping.store(true);
    if (isRunning())
        wait();
}

void EmuThread::run()
{
    SDL_AudioDeviceID audioDevice = 0;
    SDL_GameController* controller = nullptr;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) == 0) {
        SDL_AudioSpec wanted {};
        wanted.freq = 48000;
        wanted.format = AUDIO_S16SYS;
        wanted.channels = 2;
        wanted.samples = 1024;
        audioDevice = SDL_OpenAudioDevice(nullptr, 0, &wanted, nullptr, 0);
        if (audioDevice)
            SDL_PauseAudioDevice(audioDevice, 0);
        for (int i = 0; i < SDL_NumJoysticks() && !controller; ++i) {
            if (SDL_IsGameController(i))
                controller = SDL_GameControllerOpen(i);
        }
    }

    QElapsedTimer frameClock;
    frameClock.start();
    while (!m_stopping.load()) {
        std::optional<SlotState> slots;
        {
            std::lock_guard lock(m_mutex);
            slots.swap(m_pendingSlots);
        }
        if (slots) {
            if (audioDevice)
                SDL_ClearQueuedAudio(audioDevice);
            switchSession(*slots);
        }
        processCommand();
        SDL_GameControllerUpdate();
        std::uint32_t gamepadKeys = 0;
        if (controller && SDL_GameControllerGetAttached(controller)) {
            const auto button = [controller](SDL_GameControllerButton value) {
                return SDL_GameControllerGetButton(controller, value) != 0;
            };
            if (button(SDL_CONTROLLER_BUTTON_A)) gamepadKeys |= Button::A;
            if (button(SDL_CONTROLLER_BUTTON_B)) gamepadKeys |= Button::B;
            if (button(SDL_CONTROLLER_BUTTON_X)) gamepadKeys |= Button::X;
            if (button(SDL_CONTROLLER_BUTTON_Y)) gamepadKeys |= Button::Y;
            if (button(SDL_CONTROLLER_BUTTON_BACK)) gamepadKeys |= Button::Select;
            if (button(SDL_CONTROLLER_BUTTON_START)) gamepadKeys |= Button::Start;
            if (button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) gamepadKeys |= Button::L;
            if (button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) gamepadKeys |= Button::R;
            constexpr Sint16 deadzone = 12000;
            const Sint16 x = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
            const Sint16 y = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
            if (button(SDL_CONTROLLER_BUTTON_DPAD_LEFT) || x < -deadzone) gamepadKeys |= Button::Left;
            if (button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || x > deadzone) gamepadKeys |= Button::Right;
            if (button(SDL_CONTROLLER_BUTTON_DPAD_UP) || y < -deadzone) gamepadKeys |= Button::Up;
            if (button(SDL_CONTROLLER_BUTTON_DPAD_DOWN) || y > deadzone) gamepadKeys |= Button::Down;
        }

        if (!m_session || m_paused.load()) {
            QThread::msleep(4);
            frameClock.restart();
            continue;
        }

        TouchPoint touch;
        {
            std::lock_guard lock(m_mutex);
            touch = m_touch;
        }
        m_session->setKeys(m_keys.load(std::memory_order_relaxed) | gamepadKeys);
        m_session->setTouch(touch);
        FramePacket packet = m_session->frame();
        if (!packet.top.isNull() || !packet.bottom.isNull())
            emit frameReady(packet);

        if (audioDevice && !packet.audio.isEmpty()) {
            constexpr Uint32 maxQueued = 48000u * 2u * sizeof(std::int16_t) / 4u; // 250 ms
            if (SDL_GetQueuedAudioSize(audioDevice) > maxQueued)
                SDL_ClearQueuedAudio(audioDevice);
            SDL_QueueAudio(audioDevice, packet.audio.constData(), static_cast<Uint32>(packet.audio.size()));
        }

        if (m_session->gbaModeRequested() && !m_activeSlots.slot2.isEmpty() && m_activeSlots.slot2Type == RomType::Gba) {
            SlotState handoff = m_activeSlots;
            handoff.slot1.clear();
            handoff.slot1Type = RomType::Unknown;
            emit osdRequested(QStringLiteral("Firmware selected GBA mode — handing Slot-2 to mGBA"));
            switchSession(handoff);
            continue;
        }

        if (!m_fastForward.load()) {
            const qint64 remaining = 16667 - frameClock.nsecsElapsed() / 1000;
            if (remaining > 0)
                QThread::usleep(static_cast<unsigned long>(remaining));
        }
        frameClock.restart();
    }

    if (m_session) {
        QString error;
        if (!m_session->flushSave(error) && !error.isEmpty())
            emit errorRaised(error);
        m_session->stop();
        m_session.reset();
    }
    if (controller)
        SDL_GameControllerClose(controller);
    if (audioDevice)
        SDL_CloseAudioDevice(audioDevice);
    SDL_QuitSubSystem(SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER);
}

void EmuThread::switchSession(const SlotState& slots)
{
    if (m_session) {
        QString error;
        if (!m_session->flushSave(error))
            emit errorRaised(error);
        m_session->stop();
        m_session.reset();
    }
    m_activeSlots = slots;

    const ActiveCore desired = slots.desiredCore();
    if (desired == ActiveCore::Mgba)
        m_session = std::make_unique<MgbaSession>(m_firmware);
    else if (desired == ActiveCore::Melon)
        m_session = std::make_unique<MelonSession>(m_firmware);
    else {
        emit coreChanged(ActiveCore::Idle, {});
        emit osdRequested(QStringLiteral("Both cartridge slots are empty"));
        return;
    }

    QString error;
    if (!m_session->start(slots, error)) {
        m_session.reset();
        emit coreChanged(ActiveCore::Idle, {});
        emit errorRaised(error);
        return;
    }
    emit coreChanged(desired, m_session->title());
    emit osdRequested(desired == ActiveCore::Mgba
        ? QStringLiteral("Now playing on Game Boy Advance")
        : QStringLiteral("Switched to Nintendo DS"));
    if (desired == ActiveCore::Melon && !slots.slot2.isEmpty() && slots.slot2Type != RomType::Gba)
        emit osdRequested(QStringLiteral("GB/GBC cartridges are not visible to DS software; Slot-1 remains active"));
}

void EmuThread::processCommand()
{
    std::optional<Command> command;
    {
        std::lock_guard lock(m_mutex);
        command.swap(m_command);
    }
    if (!command)
        return;
    if (!m_session) {
        emit stateOperationFinished(false, QStringLiteral("No game is running."));
        return;
    }

    if (command->type == CommandType::Reset) {
        m_session->reset();
        emit osdRequested(QStringLiteral("Console reset"));
        return;
    }

    QString error;
    const bool ok = command->type == CommandType::SaveState
        ? m_session->saveState(command->path, error)
        : m_session->loadState(command->path, error);
    const QString verb = command->type == CommandType::SaveState ? QStringLiteral("saved") : QStringLiteral("loaded");
    emit stateOperationFinished(ok, ok ? QStringLiteral("State %1").arg(verb) : error);
}

} // namespace dualslot
