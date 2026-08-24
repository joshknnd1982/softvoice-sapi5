#include "sv_com.hpp"

#include <algorithm>

namespace SoftVoice {
namespace com {

wchar_t* strdup(const std::wstring& s)
{
    const std::size_t size = s.size();
    auto* buffer = static_cast<wchar_t*>(
        CoTaskMemAlloc((size + 1) * sizeof(wchar_t)));
    if (!buffer) {
        throw std::bad_alloc();
    }
    std::copy(s.begin(), s.end(), buffer);
    buffer[size] = L'\0';
    return buffer;
}

std::atomic<long> object_counter::count_{0};

HRESULT class_object_factory::create(REFCLSID rclsid, REFIID riid,
                                     void** ppv) const noexcept
{
    if (!ppv) {
        return E_POINTER;
    }
    *ppv = nullptr;

    for (const auto& creator : creators_) {
        if (creator->matches(rclsid)) {
            try {
                return creator->create(riid, ppv);
            }
            catch (const std::bad_alloc&) {
                return E_OUTOFMEMORY;
            }
            catch (...) {
                return E_UNEXPECTED;
            }
        }
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

class_registrar::class_registrar(HINSTANCE dll_handle)
{
    wchar_t buffer[MAX_PATH + 1] = {};
    const DWORD size = GetModuleFileNameW(dll_handle, buffer, MAX_PATH);
    if (size == 0) {
        throw std::runtime_error("Unable to determine the path of the DLL");
    }
    buffer[size] = L'\0';
    dll_path_.assign(buffer);
}

// Under HKLM rather than HKCU on purpose: SAPI's TokenEnums enumeration is
// read from HKLM only, so an engine registered per-user registers cleanly
// and is then never enumerated.
const std::wstring class_registrar::clsid_key_path(L"Software\\Classes\\CLSID");

}  // namespace com
}  // namespace SoftVoice
