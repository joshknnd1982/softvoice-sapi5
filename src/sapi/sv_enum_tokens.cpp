#include "sv_enum_tokens.hpp"

#include <algorithm>
#include <new>
#include <stdexcept>

#include "sv_log.hpp"
#include "sv_paths.hpp"

namespace SoftVoice {
namespace sapi {

IEnumSpObjectTokensImpl::IEnumSpObjectTokensImpl(bool initialize)
{
    if (!initialize) {
        return;
    }
    voices_ = SoftVoice::available_voices();
    SV_LOG_DEBUG("enum: publishing %zu voices", voices_.size());
}

IEnumSpObjectTokensImpl::ISpObjectTokenPtr
IEnumSpObjectTokensImpl::create_token(const Voice& voice) const
{
    const std::wstring token_id = std::wstring(SPCAT_VOICES) +
                                  L"\\TokenEnums\\SoftVoice\\" + voice.name();

    com::object<voice_token> data_key(voice);
    com::interface_ptr<ISpDataKey> data_key_ptr(data_key);

    ISpObjectTokenInitPtr init(CLSID_SpObjectToken);
    if (!init) {
        throw std::runtime_error("Unable to create a SAPI object token");
    }
    if (FAILED(init->InitFromDataKey(SPCAT_VOICES, token_id.c_str(),
                                     data_key_ptr.get(false)))) {
        throw std::runtime_error("Unable to initialise a SAPI object token");
    }
    return ISpObjectTokenPtr(init);
}

STDMETHODIMP IEnumSpObjectTokensImpl::Next(ULONG count,
                                           ISpObjectToken** tokens,
                                           ULONG* fetched)
{
    if (count == 0) {
        return E_INVALIDARG;
    }
    if (!tokens) {
        return E_POINTER;
    }
    if (!fetched && count > 1) {
        return E_POINTER;
    }
    if (fetched) {
        *fetched = 0;
    }

    try {
        std::vector<ISpObjectTokenPtr> created;
        created.reserve(count);

        const std::size_t last =
            (std::min)(index_ + static_cast<std::size_t>(count),
                       voices_.size());
        for (std::size_t i = index_; i < last; ++i) {
            created.push_back(create_token(voices_[i]));
        }

        for (std::size_t i = 0; i < created.size(); ++i) {
            created[i].AddRef();
            tokens[i] = created[i].GetInterfacePtr();
        }
        if (fetched) {
            *fetched = static_cast<ULONG>(created.size());
        }
        index_ += created.size();
        return created.size() == count ? S_OK : S_FALSE;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        SV_LOG_ERROR("enum: Next failed while building a token");
        return E_UNEXPECTED;
    }
}

STDMETHODIMP IEnumSpObjectTokensImpl::Skip(ULONG count)
{
    const std::size_t remaining = voices_.size() - index_;
    const std::size_t skipped =
        (std::min)(remaining, static_cast<std::size_t>(count));
    index_ += skipped;
    return skipped == count ? S_OK : S_FALSE;
}

STDMETHODIMP IEnumSpObjectTokensImpl::Reset()
{
    index_ = 0;
    return S_OK;
}

STDMETHODIMP IEnumSpObjectTokensImpl::GetCount(ULONG* count)
{
    if (!count) {
        return E_POINTER;
    }
    *count = static_cast<ULONG>(voices_.size());
    return S_OK;
}

STDMETHODIMP IEnumSpObjectTokensImpl::Item(ULONG index,
                                           ISpObjectToken** token)
{
    if (!token) {
        return E_POINTER;
    }
    *token = nullptr;

    if (index >= voices_.size()) {
        return SPERR_NO_MORE_ITEMS;
    }

    try {
        ISpObjectTokenPtr created = create_token(voices_[index]);
        created.AddRef();
        *token = created.GetInterfacePtr();
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}

STDMETHODIMP IEnumSpObjectTokensImpl::Clone(IEnumSpObjectTokens** enumerator)
{
    if (!enumerator) {
        return E_POINTER;
    }
    *enumerator = nullptr;

    try {
        com::object<IEnumSpObjectTokensImpl> clone(false);
        clone->voices_ = voices_;
        clone->index_ = index_;
        com::interface_ptr<IEnumSpObjectTokens> pointer(clone);
        *enumerator = pointer.get();
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
