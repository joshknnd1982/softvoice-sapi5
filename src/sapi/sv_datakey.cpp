#include "sv_datakey.hpp"

#include <iterator>

namespace SoftVoice {
namespace sapi {

STDMETHODIMP ISpDataKeyImpl::GetData(LPCWSTR, ULONG*, BYTE*)
{
    return SPERR_NOT_FOUND;
}

STDMETHODIMP ISpDataKeyImpl::GetStringValue(LPCWSTR name, LPWSTR* value)
{
    if (!value) {
        return E_POINTER;
    }
    *value = nullptr;

    try {
        if (!name || name[0] == L'\0') {
            *value = com::strdup(default_value_);
        } else {
            const auto it = values_.find(name);
            if (it == values_.end()) {
                return SPERR_NOT_FOUND;
            }
            *value = com::strdup(it->second);
        }
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}

STDMETHODIMP ISpDataKeyImpl::GetDWORD(LPCWSTR, DWORD*)
{
    return E_NOTIMPL;
}

STDMETHODIMP ISpDataKeyImpl::OpenKey(LPCWSTR, ISpDataKey**)
{
    return SPERR_NOT_FOUND;
}

STDMETHODIMP ISpDataKeyImpl::EnumKeys(ULONG, LPWSTR*)
{
    return SPERR_NO_MORE_ITEMS;
}

STDMETHODIMP ISpDataKeyImpl::EnumValues(ULONG index, LPWSTR* name)
{
    if (index >= values_.size()) {
        return SPERR_NO_MORE_ITEMS;
    }
    if (!name) {
        return E_POINTER;
    }
    *name = nullptr;

    try {
        auto it = values_.begin();
        std::advance(it, index);
        *name = com::strdup(it->first);
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}

STDMETHODIMP ISpDataKeyImpl::SetData(LPCWSTR, ULONG, const BYTE*)
{
    return E_NOTIMPL;
}

STDMETHODIMP ISpDataKeyImpl::SetStringValue(LPCWSTR, LPCWSTR)
{
    return E_NOTIMPL;
}

STDMETHODIMP ISpDataKeyImpl::SetDWORD(LPCWSTR, DWORD)
{
    return E_NOTIMPL;
}

STDMETHODIMP ISpDataKeyImpl::CreateKey(LPCWSTR, ISpDataKey**)
{
    return E_NOTIMPL;
}

STDMETHODIMP ISpDataKeyImpl::DeleteKey(LPCWSTR)
{
    return E_NOTIMPL;
}

STDMETHODIMP ISpDataKeyImpl::DeleteValue(LPCWSTR)
{
    return E_NOTIMPL;
}

}  // namespace sapi
}  // namespace SoftVoice
