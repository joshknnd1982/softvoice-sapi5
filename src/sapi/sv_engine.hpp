#pragma once

// The SAPI 5 TTS engine.
//
// Identical source for the 32-bit and the 64-bit build. Both spawn the same
// 32-bit svwebspeak-host.exe and talk to it over a loopback socket, so
// unlike most dual-architecture speech wrappers there is no bitness-specific
// code path, no COM surrogate and no second helper to keep in step.

#include <comdef.h>
#include <comip.h>
#include <memory>
#include <sapi.h>
#include <sapiddk.h>
#include <windows.h>

#include "sv_com.hpp"
#include "sv_settings.hpp"
#include "sv_synth.hpp"

namespace SoftVoice {
namespace sapi {

class __declspec(uuid("{B6E31F84-5A0D-4C72-9F13-8D4A7C25E0B9}"))
    ISpTTSEngineImpl : public ISpTTSEngine, public ISpObjectWithToken {
public:
    ISpTTSEngineImpl();
    ~ISpTTSEngineImpl();

    ISpTTSEngineImpl(const ISpTTSEngineImpl&) = delete;
    ISpTTSEngineImpl& operator=(const ISpTTSEngineImpl&) = delete;

    STDMETHOD(Speak)
    (DWORD flags, REFGUID format_id, const WAVEFORMATEX* wave_format,
     const SPVTEXTFRAG* fragments, ISpTTSEngineSite* site) override;
    STDMETHOD(GetOutputFormat)
    (const GUID* target_id, const WAVEFORMATEX* target_format,
     GUID* output_id, WAVEFORMATEX** output_format) override;

    STDMETHOD(SetObjectToken)(ISpObjectToken* token) override;
    STDMETHOD(GetObjectToken)(ISpObjectToken** token) override;

protected:
    [[nodiscard]] void* get_interface(REFIID riid) noexcept
    {
        void* ptr = com::try_primary_interface<ISpTTSEngine>(this, riid);
        return ptr ? ptr : com::try_interface<ISpObjectWithToken>(this, riid);
    }

private:
    _COM_SMARTPTR_TYPEDEF(ISpObjectToken, __uuidof(ISpObjectToken));
    _COM_SMARTPTR_TYPEDEF(ISpDataKey, __uuidof(ISpDataKey));

    ISpObjectTokenPtr token_;
    int voice_id_ = 0;

    SettingsWatcher settings_;
    Synth synth_;

    // The format this stream was opened with. GetOutputFormat fixes it, and
    // the host has to be opened to match: the engine chooses its format at
    // SVOpenSpeech and cannot change it afterwards.
    unsigned stream_rate_ = 22050;
};

}  // namespace sapi
}  // namespace SoftVoice
