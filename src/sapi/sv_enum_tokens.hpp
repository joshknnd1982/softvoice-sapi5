#pragma once

// The voice enumerator SAPI calls to list the SoftVoice voices.
//
// Registered once under HKLM\SOFTWARE\Microsoft\Speech\Voices\TokenEnums, it
// hands SAPI forty tokens built from the compiled-in catalogue. That key is
// read from HKLM only - an enumerator registered per user registers
// perfectly and is then never consulted.

#include <comdef.h>
#include <comip.h>
#include <sapi.h>
#include <sapiddk.h>
#include <sperror.h>
#include <vector>
#include <windows.h>

#include "sv_com.hpp"
#include "sv_token.hpp"
#include "sv_voices.hpp"

namespace SoftVoice {
namespace sapi {

class __declspec(uuid("{7C4A9E51-2D68-4B3F-9E07-C1A5B8D2F640}"))
    IEnumSpObjectTokensImpl : public IEnumSpObjectTokens {
public:
    explicit IEnumSpObjectTokensImpl(bool initialize = true);

    IEnumSpObjectTokensImpl(const IEnumSpObjectTokensImpl&) = delete;
    IEnumSpObjectTokensImpl& operator=(const IEnumSpObjectTokensImpl&) = delete;

    STDMETHOD(Next)
    (ULONG count, ISpObjectToken** tokens, ULONG* fetched) override;
    STDMETHOD(Skip)(ULONG count) override;
    STDMETHOD(Reset)() override;
    STDMETHOD(Clone)(IEnumSpObjectTokens** enumerator) override;
    STDMETHOD(Item)(ULONG index, ISpObjectToken** token) override;
    STDMETHOD(GetCount)(ULONG* count) override;

protected:
    [[nodiscard]] void* get_interface(REFIID riid) noexcept
    {
        return com::try_primary_interface<IEnumSpObjectTokens>(this, riid);
    }

private:
    _COM_SMARTPTR_TYPEDEF(ISpObjectToken, __uuidof(ISpObjectToken));
    _COM_SMARTPTR_TYPEDEF(ISpObjectTokenInit, __uuidof(ISpObjectTokenInit));

    [[nodiscard]] ISpObjectTokenPtr create_token(const Voice& voice) const;

    std::size_t index_ = 0;
    std::vector<Voice> voices_;
};

}  // namespace sapi
}  // namespace SoftVoice
