#include "MelonPlatformBridge.h"

#include "Platform.h"
#include "SPI_Firmware.h"

#include <QCoreApplication>
#include <QDir>

#include <SDL_loadso.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>

namespace melonDS::Platform {

struct FileHandle { std::FILE* file = nullptr; };
struct Thread { std::thread worker; };
struct Semaphore {
    std::mutex mutex;
    std::condition_variable changed;
    int count = 0;
};
struct Mutex { std::mutex mutex; };
struct AACDecoder {};
struct DynamicLibrary { void* handle = nullptr; };

namespace {
const auto startTime = std::chrono::steady_clock::now();

dualslot::MelonPlatformBridge* bridge(void* userdata)
{
    return static_cast<dualslot::MelonPlatformBridge*>(userdata);
}

const char* modeString(FileMode mode, bool exists)
{
    const bool read = (mode & Read) != None;
    const bool write = (mode & Write) != None;
    const bool append = (mode & Append) != None;
    const bool preserve = (mode & Preserve) != None;
    if (append)
        return read ? "a+b" : "ab";
    if (read && write)
        return preserve && exists ? "r+b" : "w+b";
    if (write)
        return preserve && exists ? "r+b" : "wb";
    return "rb";
}
} // namespace

void SignalStop(StopReason reason, void* userdata)
{
    if (auto* target = bridge(userdata))
        target->melonStopped(static_cast<int>(reason));
}

std::string GetLocalFilePath(const std::string& filename)
{
    const QString path = QString::fromUtf8(filename);
    if (QDir::isAbsolutePath(path))
        return path.toStdString();
    return QDir(QCoreApplication::applicationDirPath()).filePath(path).toStdString();
}

FileHandle* OpenFile(const std::string& path, FileMode mode)
{
    if ((mode & (ReadWrite | Append)) == None)
        return nullptr;
    const std::filesystem::path native = std::filesystem::u8path(path);
    const bool exists = std::filesystem::exists(native);
    if ((mode & NoCreate) != None && !exists)
        return nullptr;
    if ((mode & Write) != None && (mode & NoCreate) == None) {
        std::error_code ec;
        if (native.has_parent_path())
            std::filesystem::create_directories(native.parent_path(), ec);
    }
#ifdef _WIN32
    const char* modeChars = modeString(mode, exists);
    const std::wstring wideMode(modeChars, modeChars + std::strlen(modeChars));
    std::FILE* file = _wfopen(native.wstring().c_str(), wideMode.c_str());
#else
    std::FILE* file = std::fopen(native.string().c_str(), modeString(mode, exists));
#endif
    return file ? new FileHandle{file} : nullptr;
}

FileHandle* OpenLocalFile(const std::string& path, FileMode mode) { return OpenFile(GetLocalFilePath(path), mode); }
bool FileExists(const std::string& name) { return std::filesystem::exists(std::filesystem::u8path(name)); }
bool LocalFileExists(const std::string& name) { return FileExists(GetLocalFilePath(name)); }

bool CheckFileWritable(const std::string& filepath)
{
    const auto path = std::filesystem::u8path(filepath);
    if (std::filesystem::exists(path)) {
        if (FileHandle* file = OpenFile(filepath, static_cast<FileMode>(Write | Preserve | NoCreate))) {
            CloseFile(file);
            return true;
        }
        return false;
    }
    std::error_code ec;
    auto parent = path.parent_path();
    if (parent.empty()) parent = std::filesystem::current_path();
    return std::filesystem::exists(parent, ec) && !ec;
}

bool CheckLocalFileWritable(const std::string& filepath) { return CheckFileWritable(GetLocalFilePath(filepath)); }

bool CloseFile(FileHandle* file)
{
    if (!file) return false;
    const bool ok = std::fclose(file->file) == 0;
    delete file;
    return ok;
}
bool IsEndOfFile(FileHandle* file) { return !file || std::feof(file->file); }
bool FileReadLine(char* str, int count, FileHandle* file) { return file && std::fgets(str, count, file->file); }
u64 FilePosition(FileHandle* file) { return file ? static_cast<u64>(std::ftell(file->file)) : 0; }
bool FileSeek(FileHandle* file, s64 offset, FileSeekOrigin origin)
{
    if (!file) return false;
    const int whence = origin == FileSeekOrigin::Start ? SEEK_SET : origin == FileSeekOrigin::Current ? SEEK_CUR : SEEK_END;
#ifdef _WIN32
    return _fseeki64(file->file, offset, whence) == 0;
#else
    return fseeko(file->file, offset, whence) == 0;
#endif
}
void FileRewind(FileHandle* file) { if (file) std::rewind(file->file); }
u64 FileRead(void* data, u64 size, u64 count, FileHandle* file) { return file ? std::fread(data, static_cast<size_t>(size), static_cast<size_t>(count), file->file) : 0; }
bool FileFlush(FileHandle* file) { return file && std::fflush(file->file) == 0; }
u64 FileWrite(const void* data, u64 size, u64 count, FileHandle* file) { return file ? std::fwrite(data, static_cast<size_t>(size), static_cast<size_t>(count), file->file) : 0; }
u64 FileWriteFormatted(FileHandle* file, const char* fmt, ...)
{
    if (!file || !fmt) return 0;
    va_list args;
    va_start(args, fmt);
    const int result = std::vfprintf(file->file, fmt, args);
    va_end(args);
    return result > 0 ? static_cast<u64>(result) : 0;
}
u64 FileLength(FileHandle* file)
{
    if (!file) return 0;
    const auto old = FilePosition(file);
    FileSeek(file, 0, FileSeekOrigin::End);
    const auto length = FilePosition(file);
    FileSeek(file, static_cast<s64>(old), FileSeekOrigin::Start);
    return length;
}

void Log(LogLevel level, const char* fmt, ...)
{
    if (level == LogLevel::Debug)
        return;
    static const char* labels[] = {"debug", "info", "warning", "error"};
    static std::mutex logMutex;
    std::lock_guard lock(logMutex);
    const char* label = labels[std::clamp(static_cast<int>(level), 0, 3)];
    va_list args;
    va_start(args, fmt);
    va_list fileArgs;
    va_copy(fileArgs, args);
    std::fprintf(stderr, "[melonDS:%s] ", label);
    std::vfprintf(stderr, fmt, args);
    std::fflush(stderr);
    if (std::FILE* file = std::fopen(GetLocalFilePath("dualslot.log").c_str(), "ab")) {
        std::fprintf(file, "[melonDS:%s] ", label);
        std::vfprintf(file, fmt, fileArgs);
        std::fclose(file);
    }
    va_end(fileArgs);
    va_end(args);
}

Thread* Thread_Create(std::function<void()> func) { return new Thread{std::thread(std::move(func))}; }
void Thread_Free(Thread* thread) { if (!thread) return; if (thread->worker.joinable()) thread->worker.join(); delete thread; }
void Thread_Wait(Thread* thread) { if (thread && thread->worker.joinable()) thread->worker.join(); }
Semaphore* Semaphore_Create() { return new Semaphore; }
void Semaphore_Free(Semaphore* sema) { delete sema; }
void Semaphore_Reset(Semaphore* sema) { std::lock_guard lock(sema->mutex); sema->count = 0; }
void Semaphore_Wait(Semaphore* sema) { std::unique_lock lock(sema->mutex); sema->changed.wait(lock, [&]{ return sema->count > 0; }); --sema->count; }
bool Semaphore_TryWait(Semaphore* sema, int timeout_ms)
{
    std::unique_lock lock(sema->mutex);
    if (!sema->changed.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&]{ return sema->count > 0; })) return false;
    --sema->count;
    return true;
}
void Semaphore_Post(Semaphore* sema, int count) { { std::lock_guard lock(sema->mutex); sema->count += count; } sema->changed.notify_all(); }
Mutex* Mutex_Create() { return new Mutex; }
void Mutex_Free(Mutex* mutex) { delete mutex; }
void Mutex_Lock(Mutex* mutex) { mutex->mutex.lock(); }
void Mutex_Unlock(Mutex* mutex) { mutex->mutex.unlock(); }
bool Mutex_TryLock(Mutex* mutex) { return mutex->mutex.try_lock(); }
void Sleep(u64 usecs) { std::this_thread::sleep_for(std::chrono::microseconds(usecs)); }
u64 GetMSCount() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count(); }
u64 GetUSCount() { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startTime).count(); }

void WriteNDSSave(const u8* data, u32 length, u32, u32, void* userdata) { if (auto* b = bridge(userdata)) b->writeNdsSave(data, length); }
void WriteGBASave(const u8* data, u32 length, u32, u32, void* userdata) { if (auto* b = bridge(userdata)) b->writeGbaSave(data, length); }
void WriteFirmware(const Firmware& firmware, u32, u32, void* userdata) { if (auto* b = bridge(userdata)) b->writeFirmware(firmware.Buffer(), firmware.Length()); }
void WriteDateTime(int, int, int, int, int, int, void*) {}

void MP_Begin(void*) {}
void MP_End(void*) {}
int MP_SendPacket(u8*, int, u64, void*) { return 0; }
int MP_RecvPacket(u8*, u64*, void*) { return 0; }
int MP_SendCmd(u8*, int, u64, void*) { return 0; }
int MP_SendReply(u8*, int, u64, u16, void*) { return 0; }
int MP_SendAck(u8*, int, u64, void*) { return 0; }
int MP_RecvHostPacket(u8*, u64*, void*) { return 0; }
u16 MP_RecvReplies(u8*, u64, u16, void*) { return 0; }
int Net_SendPacket(u8*, int, void*) { return 0; }
int Net_RecvPacket(u8*, void*) { return 0; }
void Camera_Start(int, void*) {}
void Camera_Stop(int, void*) {}
void Camera_CaptureFrame(int, u32* frame, int width, int height, bool, void*) { if (frame) std::fill(frame, frame + width * height, 0xFF000000u); }
void Mic_Start(void*) {}
void Mic_Stop(void*) {}
int Mic_ReadInput(s16* data, int maxlength, void*) { if (data) std::fill(data, data + maxlength, 0); return maxlength; }
AACDecoder* AAC_Init() { return nullptr; }
void AAC_DeInit(AACDecoder* dec) { delete dec; }
bool AAC_Configure(AACDecoder*, int, int) { return false; }
bool AAC_DecodeFrame(AACDecoder*, const void*, int, void*, int) { return false; }
bool Addon_KeyDown(KeyType, void*) { return false; }
void Addon_RumbleStart(u32, void*) {}
void Addon_RumbleStop(void*) {}
float Addon_MotionQuery(MotionQueryType, void*) { return 0.0f; }
DynamicLibrary* DynamicLibrary_Load(const char* lib) { void* h = SDL_LoadObject(lib); return h ? new DynamicLibrary{h} : nullptr; }
void DynamicLibrary_Unload(DynamicLibrary* lib) { if (lib) { SDL_UnloadObject(lib->handle); delete lib; } }
void* DynamicLibrary_LoadFunction(DynamicLibrary* lib, const char* name) { return lib ? SDL_LoadFunction(lib->handle, name) : nullptr; }

} // namespace melonDS::Platform
