#include "sv_paths.hpp"

#include <shlobj.h>

namespace SoftVoice {
namespace paths {

namespace {

// The address of a function in this module, so GetModuleHandleEx finds the
// DLL rather than the host application.
HMODULE this_module()
{
    HMODULE handle = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&this_module), &handle);
    return handle;
}

std::wstring parent_of(const std::wstring& path)
{
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(0, slash);
}

std::wstring leaf_of(const std::wstring& path)
{
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

// Build a subdirectory of a known folder, creating it along the way.
std::wstring known_folder_subdir(REFKNOWNFOLDERID id, const wchar_t* sub)
{
    PWSTR base = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &base)) || !base) {
        if (base) {
            CoTaskMemFree(base);
        }
        // Falling back to the temp directory keeps logging and settings
        // working on a locked-down profile instead of silently vanishing.
        wchar_t temp[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, temp);
        std::wstring result(temp);
        if (!result.empty() && result.back() == L'\\') {
            result.pop_back();
        }
        return result;
    }
    std::wstring result(base);
    CoTaskMemFree(base);

    // Create each component in turn; the leaf may be nested.
    std::wstring remaining(sub);
    while (!remaining.empty()) {
        const std::size_t slash = remaining.find(L'\\');
        const std::wstring part = remaining.substr(0, slash);
        result += L'\\';
        result += part;
        CreateDirectoryW(result.c_str(), nullptr);
        if (slash == std::wstring::npos) {
            break;
        }
        remaining = remaining.substr(slash + 1);
    }
    return result;
}

}  // namespace

bool file_exists(const std::wstring& path)
{
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES &&
           !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring module_dir(HMODULE module)
{
    if (!module) {
        module = this_module();
    }
    wchar_t buffer[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW(module, buffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return std::wstring();
    }
    return parent_of(std::wstring(buffer, len));
}

std::wstring engine_dir(HMODULE module)
{
    const std::wstring dir = module_dir(module);
    if (dir.empty()) {
        return dir;
    }
    // The 64-bit DLL ships in x64\ beside the 32-bit one; the host and the
    // engine DLLs are 32-bit and are not duplicated.
    if (_wcsicmp(leaf_of(dir).c_str(), L"x64") == 0) {
        return parent_of(dir);
    }
    return dir;
}

std::wstring host_exe(HMODULE module)
{
    const std::wstring dir = engine_dir(module);
    if (dir.empty()) {
        return std::wstring();
    }
    std::wstring candidate = dir + L"\\svwebspeak-host.exe";
    if (file_exists(candidate)) {
        return candidate;
    }
    // A build tree keeps the binaries in bin\ next to the compiler output.
    candidate = dir + L"\\bin\\svwebspeak-host.exe";
    if (file_exists(candidate)) {
        return candidate;
    }
    candidate = parent_of(dir) + L"\\bin\\svwebspeak-host.exe";
    if (file_exists(candidate)) {
        return candidate;
    }
    return std::wstring();
}

std::wstring log_dir()
{
    return known_folder_subdir(FOLDERID_LocalAppData, L"SoftVoice SAPI5\\Logs");
}

std::wstring settings_file()
{
    return known_folder_subdir(FOLDERID_RoamingAppData, L"SoftVoice SAPI5") +
           L"\\softvoice.ini";
}

}  // namespace paths
}  // namespace SoftVoice
