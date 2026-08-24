#pragma once

// One SAPI voice token, built from the compiled-in catalogue rather than
// from a registry key.

#include <map>
#include <string>

#include "sv_datakey.hpp"
#include "sv_voices.hpp"

namespace SoftVoice {
namespace sapi {

class voice_token : public ISpDataKeyImpl {
public:
    explicit voice_token(const Voice& voice);

    STDMETHOD(OpenKey)(LPCWSTR name, ISpDataKey** key) override;
    STDMETHOD(EnumKeys)(ULONG index, LPWSTR* name) override;

private:
    std::map<std::wstring, std::wstring, str_less> attributes_;
};

}  // namespace sapi
}  // namespace SoftVoice
