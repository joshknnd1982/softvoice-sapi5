#include "sv_token.hpp"

#include <new>

#include "sv_engine.hpp"

namespace SoftVoice {
namespace sapi {

voice_token::voice_token(const Voice& voice)
{
    const std::wstring name = voice.name();
    set(name);

    utils::out_ptr<wchar_t> clsid(CoTaskMemFree);
    StringFromCLSID(__uuidof(ISpTTSEngineImpl), clsid.address());
    set(L"CLSID", clsid.get());

    // The attributes an application filters and sorts on. Language is the
    // hex LCID SAPI expects; Name is what the engine reads back in
    // SetObjectToken to work out which of the forty voices it has become.
    attributes_[L"Name"] = name;
    attributes_[L"Language"] = voice.language().lcid;
    attributes_[L"Gender"] = voice.personality().gender;
    attributes_[L"Age"] = voice.personality().age;
    attributes_[L"Vendor"] = L"SoftVoice";
    attributes_[L"SharedPronunciation"] = L"";
}

STDMETHODIMP voice_token::OpenKey(LPCWSTR name, ISpDataKey** key)
{
    if (!name) {
        return E_INVALIDARG;
    }
    if (!key) {
        return E_POINTER;
    }
    *key = nullptr;

    try {
        if (_wcsicmp(name, L"Attributes") != 0) {
            return SPERR_NOT_FOUND;
        }

        com::object<ISpDataKeyImpl> obj;
        for (const auto& [attribute, value] : attributes_) {
            obj->set(attribute, value);
        }

        com::interface_ptr<ISpDataKey> pointer(obj);
        *key = pointer.get();
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}

STDMETHODIMP voice_token::EnumKeys(ULONG index, LPWSTR* name)
{
    if (!name) {
        return E_POINTER;
    }
    *name = nullptr;

    if (index > 0) {
        return SPERR_NO_MORE_ITEMS;
    }

    try {
        *name = com::strdup(L"Attributes");
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}

}  // namespace sapi
}  // namespace SoftVoice
