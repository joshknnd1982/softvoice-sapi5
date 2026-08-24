#pragma once

// An in-memory ISpDataKey.
//
// This is what keeps the forty voices out of the registry. SAPI normally
// reads a voice token's attributes from HKLM\...\Speech\Voices\<name>; here
// the enumerator builds a token from one of these instead, so installing
// writes nothing per voice and adding a voice needs no registry change.

#include <map>
#include <sapi.h>
#include <sapiddk.h>
#include <sperror.h>
#include <string>
#include <windows.h>

#include "sv_com.hpp"

namespace SoftVoice {
namespace sapi {

class ISpDataKeyImpl : public ISpDataKey {
public:
    STDMETHOD(GetData)
    (LPCWSTR name, ULONG* size, BYTE* data) override;
    STDMETHOD(GetStringValue)(LPCWSTR name, LPWSTR* value) override;
    STDMETHOD(GetDWORD)(LPCWSTR name, DWORD* value) override;
    STDMETHOD(OpenKey)(LPCWSTR name, ISpDataKey** key) override;
    STDMETHOD(EnumKeys)(ULONG index, LPWSTR* name) override;
    STDMETHOD(EnumValues)(ULONG index, LPWSTR* name) override;
    STDMETHOD(SetData)(LPCWSTR name, ULONG size, const BYTE* data) override;
    STDMETHOD(SetStringValue)(LPCWSTR name, LPCWSTR value) override;
    STDMETHOD(SetDWORD)(LPCWSTR name, DWORD value) override;
    STDMETHOD(CreateKey)(LPCWSTR name, ISpDataKey** key) override;
    STDMETHOD(DeleteKey)(LPCWSTR name) override;
    STDMETHOD(DeleteValue)(LPCWSTR name) override;

    void set(const std::wstring& name, const std::wstring& value)
    {
        values_[name] = value;
    }

    void set(const std::wstring& value) { default_value_ = value; }

protected:
    struct str_less {
        [[nodiscard]] bool operator()(const std::wstring& a,
                                      const std::wstring& b) const noexcept
        {
            return _wcsicmp(a.c_str(), b.c_str()) < 0;
        }
    };

    [[nodiscard]] void* get_interface(REFIID riid) noexcept
    {
        return com::try_primary_interface<ISpDataKey>(this, riid);
    }

private:
    std::wstring default_value_;
    std::map<std::wstring, std::wstring, str_less> values_;
};

}  // namespace sapi
}  // namespace SoftVoice
