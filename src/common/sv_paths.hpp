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

// Read a whole file as UTF-8 and widen it, tolerating a byte order mark.
// False if the file cannot be read, or is far larger than any of ours.
//
// Both text files this package reads - the per-user settings and the
// installed voice selection - are written by this code but documented as
// editable by hand, so the parsing has to survive whatever an editor does to
// them. A byte order mark is the usual thing, and it is also why neither is
// read with GetPrivateProfileString: the profile API recognises only a
// UTF-16 mark, and quietly treats a UTF-8 one as part of the first section
// name, which turns a hand-edited file into one that appears to say nothing
// at all.
[[nodiscard]] bool read_text_file(const std::wstring& path, std::wstring* out);

}  // namespace paths
}  // namespace SoftVoice
