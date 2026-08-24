#include "sv_log.hpp"

#include <cstdarg>
#include <cstdio>
#include <cwchar>

#include "sv_paths.hpp"

namespace SoftVoice {
namespace log {

namespace {

CRITICAL_SECTION g_lock;
bool g_lock_ready = false;
HANDLE g_file = INVALID_HANDLE_VALUE;
std::wstring g_path;
Level g_level = Level::Info;

// Guards the one-time CRITICAL_SECTION setup. init() is called from DllMain
// and from main(), and the utility's preview thread logs too, so the lock
// itself has to be created race-free.
INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

BOOL CALLBACK create_lock(PINIT_ONCE, PVOID, PVOID*)
{
    InitializeCriticalSection(&g_lock);
    g_lock_ready = true;
    return TRUE;
}

void ensure_lock()
{
    InitOnceExecuteOnce(&g_once, create_lock, nullptr, nullptr);
}

std::wstring exe_leaf()
{
    wchar_t buffer[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, buffer, MAX_PATH) == 0) {
        return L"host";
    }
    std::wstring path(buffer);
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        path = path.substr(slash + 1);
    }
    const std::size_t dot = path.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        path = path.substr(0, dot);
    }
    // Keep the name usable as a filename component.
    for (wchar_t& c : path) {
        if (wcschr(L"\\/:*?\"<>| ", c)) {
            c = L'_';
        }
    }
    return path.empty() ? L"host" : path;
}

void write_raw(const char* text, std::size_t length)
{
    if (g_file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(g_file, text, static_cast<DWORD>(length), &written, nullptr);
}

}  // namespace

Level level_from_string(const std::wstring& text) noexcept
{
    if (_wcsicmp(text.c_str(), L"off") == 0) return Level::Off;
    if (_wcsicmp(text.c_str(), L"error") == 0) return Level::Error;
    if (_wcsicmp(text.c_str(), L"warning") == 0) return Level::Warning;
    if (_wcsicmp(text.c_str(), L"warn") == 0) return Level::Warning;
    if (_wcsicmp(text.c_str(), L"info") == 0) return Level::Info;
    if (_wcsicmp(text.c_str(), L"debug") == 0) return Level::Debug;
    if (_wcsicmp(text.c_str(), L"trace") == 0) return Level::Trace;
    return Level::Info;
}

const wchar_t* level_to_string(Level level) noexcept
{
    switch (level) {
        case Level::Off: return L"off";
        case Level::Error: return L"error";
        case Level::Warning: return L"warning";
        case Level::Info: return L"info";
        case Level::Debug: return L"debug";
        case Level::Trace: return L"trace";
    }
    return L"info";
}

void set_level(Level level) noexcept
{
    g_level = level;
}

Level current_level() noexcept
{
    return g_level;
}

std::wstring current_file()
{
    return g_path;
}

void init(const wchar_t* tag)
{
    ensure_lock();
    EnterCriticalSection(&g_lock);
    if (g_file != INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&g_lock);
        return;
    }

    const std::wstring dir = paths::log_dir();
    wchar_t name[MAX_PATH] = {};
    swprintf_s(name, L"%s\\softvoice-%s-%s-%lu.log", dir.c_str(),
               tag ? tag : L"sapi", exe_leaf().c_str(),
               GetCurrentProcessId());
    g_path = name;

    // FILE_SHARE_READ so the log can be read while it is being written -
    // a user reporting a fault should not have to stop speaking first.
    g_file = CreateFileW(g_path.c_str(), FILE_APPEND_DATA,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    LeaveCriticalSection(&g_lock);

    if (g_file == INVALID_HANDLE_VALUE) {
        return;
    }

    SYSTEMTIME now = {};
    GetLocalTime(&now);
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    write(Level::Error,
          "======== SoftVoice SAPI 5 log opened %04d-%02d-%02d %02d:%02d:%02d "
          "========",
          now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
          now.wSecond);
    write(Level::Error, "process: %S (pid %lu, %d-bit)", exe,
          GetCurrentProcessId(), static_cast<int>(sizeof(void*) * 8));
}

void shutdown()
{
    if (!g_lock_ready) {
        return;
    }
    EnterCriticalSection(&g_lock);
    if (g_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
    LeaveCriticalSection(&g_lock);
}

void write(Level level, const char* format, ...)
{
    // Errors are never filtered out: a log that omits the failure is worse
    // than no log at all.
    if (level != Level::Error && level > g_level) {
        return;
    }
    if (g_file == INVALID_HANDLE_VALUE) {
        return;
    }

    SYSTEMTIME now = {};
    GetLocalTime(&now);

    char prefix[64] = {};
    const char* tag = "info ";
    switch (level) {
        case Level::Off: tag = "off  "; break;
        case Level::Error: tag = "ERROR"; break;
        case Level::Warning: tag = "warn "; break;
        case Level::Info: tag = "info "; break;
        case Level::Debug: tag = "debug"; break;
        case Level::Trace: tag = "trace"; break;
    }
    const int prefix_len = _snprintf_s(
        prefix, sizeof(prefix), _TRUNCATE, "%02d:%02d:%02d.%03d %s [%lu] ",
        now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, tag,
        GetCurrentThreadId());

    char body[4096] = {};
    va_list args;
    va_start(args, format);
    int body_len = _vsnprintf_s(body, sizeof(body) - 2, _TRUNCATE, format, args);
    va_end(args);
    if (body_len < 0) {
        body_len = static_cast<int>(strlen(body));
    }
    body[body_len] = '\r';
    body[body_len + 1] = '\n';
    body_len += 2;

    ensure_lock();
    EnterCriticalSection(&g_lock);
    if (prefix_len > 0) {
        write_raw(prefix, static_cast<std::size_t>(prefix_len));
    }
    write_raw(body, static_cast<std::size_t>(body_len));
    LeaveCriticalSection(&g_lock);
}

}  // namespace log
}  // namespace SoftVoice
