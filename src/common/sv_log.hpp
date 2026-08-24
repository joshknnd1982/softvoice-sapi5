#pragma once

// Detailed logging for the SAPI 5 interfaces, the configuration utility and
// the diagnostic tools.
//
// One file per process per day under %LOCALAPPDATA%\SoftVoice SAPI5\Logs,
// named after the program and its process id, so a screen reader, a book
// reader and the settings utility all speaking at once produce three
// separate, complete logs instead of one interleaved mess.
//
// The level is read from the settings file, so raising it does not need a
// rebuild or an environment variable. Errors are always written, whatever
// the level, because the point of the log is to explain a failure.

#include <string>
#include <windows.h>

namespace SoftVoice {
namespace log {

enum class Level {
    Off = 0,
    Error = 1,
    Warning = 2,
    Info = 3,
    Debug = 4,
    Trace = 5,
};

[[nodiscard]] Level level_from_string(const std::wstring& text) noexcept;
[[nodiscard]] const wchar_t* level_to_string(Level level) noexcept;

// `tag` names the log file: softvoice-<tag>-<pid>.log.
void init(const wchar_t* tag);
void set_level(Level level) noexcept;
[[nodiscard]] Level current_level() noexcept;
void shutdown();

// Printf-style, ASCII format string. %s takes char*, %S takes wchar_t*.
void write(Level level, const char* format, ...);

// Full path of the file this process is logging to, for the utility's
// "open log folder" button and for error messages.
[[nodiscard]] std::wstring current_file();

}  // namespace log
}  // namespace SoftVoice

#define SV_LOG_ERROR(...) \
    ::SoftVoice::log::write(::SoftVoice::log::Level::Error, __VA_ARGS__)
#define SV_LOG_WARN(...) \
    ::SoftVoice::log::write(::SoftVoice::log::Level::Warning, __VA_ARGS__)
#define SV_LOG_INFO(...) \
    ::SoftVoice::log::write(::SoftVoice::log::Level::Info, __VA_ARGS__)
#define SV_LOG_DEBUG(...) \
    ::SoftVoice::log::write(::SoftVoice::log::Level::Debug, __VA_ARGS__)
#define SV_LOG_TRACE(...) \
    ::SoftVoice::log::write(::SoftVoice::log::Level::Trace, __VA_ARGS__)
