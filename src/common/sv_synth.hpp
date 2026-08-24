#pragma once

// One configured SoftVoice engine: a host, the voice selected in it, and the
// settings applied to that voice.
//
// Shared by the SAPI 5 interface and the configuration utility so that a
// preview in the utility and the real thing are produced by identical code.
// Parameters are only re-sent when something actually changed, because
// SVSetPersonality reloads a whole preset - it resets rate, pitch, volume
// and inflection to that personality's own values - so every parameter has
// to be re-applied after it, and doing that per utterance would be wasteful.

#include <memory>
#include <string>

#include "sv_host.hpp"
#include "sv_settings.hpp"
#include "sv_voices.hpp"

namespace SoftVoice {

// What SAPI contributes on top of the stored settings for one fragment.
struct SpeechAdjust {
    int rate = 0;      // SAPI rate, -10..+10
    int pitch = 0;     // SAPI MiddleAdj, -10..+10
    int volume = 100;  // SAPI volume, 0..100
};

class Synth {
public:
    Synth() = default;
    ~Synth() = default;

    Synth(const Synth&) = delete;
    Synth& operator=(const Synth&) = delete;

    // Bring up a host if there is not a usable one already, opened at
    // `sample_rate`. A change of sample rate replaces the host, because the
    // engine fixes its output format at SVOpenSpeech.
    bool ensure_host(unsigned sample_rate, HostError* error);

    // Select `voice_id` and apply `settings`, sending only what changed.
    void apply(int voice_id, const VoiceSettings& settings,
               const SpeechAdjust& adjust);

    // `keep_going` is polled while waiting, so an interruption is acted on
    // within milliseconds rather than at the end of the utterance.
    bool speak(const std::wstring& text, const AudioSink& sink,
               const CancelCheck& keep_going = nullptr);
    void stop();

    void close();

    [[nodiscard]] bool ready() const noexcept
    {
        return host_ && host_->alive();
    }
    [[nodiscard]] unsigned sample_rate() const noexcept
    {
        return host_ ? host_->sample_rate() : 22050;
    }
    [[nodiscard]] unsigned bits_per_sample() const noexcept
    {
        return host_ ? host_->bits_per_sample() : 16;
    }
    [[nodiscard]] unsigned languages() const noexcept
    {
        return host_ ? host_->languages() : 0;
    }
    [[nodiscard]] const std::wstring& last_error() const noexcept
    {
        return last_error_;
    }

private:
    void send_all(int voice_id, const VoiceSettings& settings,
                  const SpeechAdjust& adjust);

    std::unique_ptr<Host> host_;
    unsigned host_rate_ = 0;
    std::wstring last_error_;

    bool applied_ = false;
    int applied_voice_ = -1;
    VoiceSettings applied_settings_{};
    SpeechAdjust applied_adjust_{};
};

// The 0-100 value actually used once SAPI's own adjustment is folded in.
// SAPI's -10..+10 covers half the scale in each direction, so the extremes
// of the engine stay reachable from an application that only offers a rate
// slider.
[[nodiscard]] int combine_rate(int stored, int sapi_rate) noexcept;
[[nodiscard]] int combine_pitch(int stored, int sapi_pitch) noexcept;
[[nodiscard]] int combine_volume(int stored, int sapi_volume) noexcept;

}  // namespace SoftVoice
