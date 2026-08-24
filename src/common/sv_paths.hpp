#pragma once

// Where everything lives, resolved relative to the module that is asking.
//
// Nothing here consults the registry. The 32-bit SAPI 5 DLL sits in the
// install root next to svwebspeak-host.exe and the engine DLLs; the 64-bit
// one sits in an x64 subdirectory and looks one level up for the host, which
// is 32-bit and shared by both.

#include <string>
#include <windows.h>

namespace SoftVoice {
namespace paths {

// Directory holding the calling module (DLL or EXE), with no trailing slash.
[[nodiscard]] std::wstring module_dir(HMODULE module = nullptr);

// Directory holding svwebspeak-host.exe and the SoftVoice engine DLLs.
// This is the module directory, or its parent when the module lives in x64\.
[[nodiscard]] std::wstring engine_dir(HMODULE module = nullptr);

// Full path of the 32-bit host. Empty if it cannot be found.
[[nodiscard]] std::wstring host_exe(HMODULE module = nullptr);

// %LOCALAPPDATA%\SoftVoice SAPI5\Logs, created if missing.
[[nodiscard]] std::wstring log_dir();

// %APPDATA%\SoftVoice SAPI5\softvoice.ini, directory created if missing.
[[nodiscard]] std::wstring settings_file();

[[nodiscard]] bool file_exists(const std::wstring& path);

}  // namespace paths
}  // namespace SoftVoice
