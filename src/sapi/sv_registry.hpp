#pragma once

// A minimal RAII wrapper over the handful of registry calls COM
// registration needs.
//
// Nothing in the speech path uses this. SAPI discovers an engine through
// HKLM\SOFTWARE\Classes\CLSID and its own TokenEnums key, and there is no
// way to publish a voice without them; the SoftVoice engine itself reads no
// registry at all, and neither do the voice catalogue or the settings.

#include <stdexcept>
#include <string>
#include <windows.h>

namespace SoftVoice {
namespace registry {

class error : public std::runtime_error {
public:
    explicit error(const std::string& message) : std::runtime_error(message) {}
};

class key {
public:
    key(HKEY parent, const std::wstring& name, REGSAM access = KEY_READ,
        bool create = false)
    {
        const LONG result =
            create ? RegCreateKeyExW(parent, name.c_str(), 0, nullptr, 0,
                                     access, nullptr, &handle_, nullptr)
                   : RegOpenKeyExW(parent, name.c_str(), 0, access, &handle_);
        if (result != ERROR_SUCCESS) {
            throw error("Unable to open or create a registry key");
        }
    }

    ~key()
    {
        if (handle_) {
            RegCloseKey(handle_);
        }
    }

    key(const key&) = delete;
    key& operator=(const key&) = delete;

    key(key&& other) noexcept : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }

    [[nodiscard]] operator HKEY() const noexcept { return handle_; }

    void delete_subkey(const std::wstring& name)
    {
        if (RegDeleteKeyW(handle_, name.c_str()) != ERROR_SUCCESS) {
            throw error("Unable to delete a registry key");
        }
    }

    void set(const std::wstring& name, const std::wstring& value);

    void set(const std::wstring& value) { set(L"", value); }

private:
    HKEY handle_ = nullptr;
};

}  // namespace registry
}  // namespace SoftVoice
