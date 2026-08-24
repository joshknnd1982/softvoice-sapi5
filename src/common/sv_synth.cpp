#include "sv_synth.hpp"

#include "sv_engine_params.hpp"
#include "sv_log.hpp"

namespace SoftVoice {

namespace {

int clamp_percent(int value) noexcept
{
    return value < 0 ? 0 : (value > 100 ? 100 : value);
}

bool same(const VoiceSettings& a, const VoiceSettings& b) noexcept
{
    return a.rate == b.rate && a.pitch == b.pitch && a.volume == b.volume &&
           a.inflection == b.inflection && a.breathiness == b.breathiness &&
           a.roughness == b.roughness && a.vowel_length == b.vowel_length &&
           a.glottal_source == b.glottal_source &&
           a.intonation == b.intonation && a.voicing == b.voicing &&
           a.gender == b.gender && a.av_bias == b.av_bias &&
           a.volume_makeup == b.volume_makeup;
}

bool same(const SpeechAdjust& a, const SpeechAdjust& b) noexcept
{
    return a.rate == b.rate && a.pitch == b.pitch && a.volume == b.volume;
}

}  // namespace

int combine_rate(int stored, int sapi_rate) noexcept
{
    return clamp_percent(stored + sapi_rate * 5);
}

int combine_pitch(int stored, int sapi_pitch) noexcept
{
    return clamp_percent(stored + sapi_pitch * 5);
}

int combine_volume(int stored, int sapi_volume) noexcept
{
    return clamp_percent(clamp_percent(stored) * clamp_percent(sapi_volume) /
                         100);
}

bool Synth::ensure_host(unsigned sample_rate, HostError* error)
{
    if (host_ && host_->alive() && host_rate_ == sample_rate) {
        return true;
    }
    if (host_) {
        SV_LOG_INFO("synth: replacing the host (was %u Hz, want %u Hz, "
                    "alive %d)",
                    host_rate_, sample_rate, host_ ? host_->alive() : 0);
        host_.reset();
    }

    HostError local;
    host_ = Host::start(sample_rate, &local);
    if (!host_) {
        last_error_ = local.message;
        applied_ = false;
        return false;
    }
    host_rate_ = sample_rate;
    last_error_.clear();
    // A fresh host is on the engine's defaults, so nothing that was applied
    // to the previous one still holds.
    applied_ = false;
    return true;
}

void Synth::apply(int voice_id, const VoiceSettings& settings,
                  const SpeechAdjust& adjust)
{
    if (!host_) {
        return;
    }
    if (applied_ && applied_voice_ == voice_id &&
        same(applied_settings_, settings) && same(applied_adjust_, adjust)) {
        return;
    }
    send_all(voice_id, settings, adjust);
    applied_ = true;
    applied_voice_ = voice_id;
    applied_settings_ = settings;
    applied_adjust_ = adjust;
}

void Synth::send_all(int voice_id, const VoiceSettings& settings,
                     const SpeechAdjust& adjust)
{
    using namespace params;

    const Voice voice(static_cast<std::size_t>(voice_id < 0 ? 0 : voice_id));
    const Personality& personality = voice.personality();
    const Language& language = voice.language();

    SV_LOG_DEBUG("synth: applying voice %d (%S), rate %d pitch %d volume %d "
                 "(SAPI %+d/%+d/%d)",
                 voice_id, voice.name().c_str(), settings.rate, settings.pitch,
                 settings.volume, adjust.rate, adjust.pitch, adjust.volume);

    // Personality first, always. It loads a complete preset and resets rate,
    // pitch, volume and inflection to that personality's own values, so
    // anything sent before it would be silently thrown away. Measured on the
    // engine: rate=300 then personality=1 renders 2.97 s, but personality=1
    // then rate=300 renders 1.49 s.
    host_->set_param(kPersonality, personality.index);
    host_->set_param(kLanguage, language.bit);

    // The three an application drives directly. Each has a verified neutral
    // point, so sending them always is safe: rate 50 is this personality's
    // own measured rate, pitch 50 reproduces its preset exactly, and
    // SVSetVolume(100) was measured to be a no-op on every voice tried.
    host_->set_param(kRate,
                     rate_to_engine(combine_rate(settings.rate, adjust.rate),
                                    personality.natural_rate));
    host_->set_param(
        kPitch, pitch_to_engine(combine_pitch(settings.pitch, adjust.pitch),
                                personality.natural_pitch));
    host_->set_param(kVolume,
                     combine_volume(settings.volume, adjust.volume));

    // Everything below is sent ONLY if the user chose it. Each personality
    // is a complete preset with its own inflection, breathiness, glottal
    // source and voicing amplitude, and there is no value that means
    // "neutral" for them - so anything sent here overwrites the voice's own
    // character, and for AV bias it also pushes most of the presets past
    // full scale, which the engine wraps into an audible crackle.
    if (settings.inflection != kVoiceDefault) {
        host_->set_param(kInflection, inflection_to_engine(settings.inflection));
    }
    if (settings.breathiness != kVoiceDefault) {
        host_->set_param(kBreath, breath_to_engine(settings.breathiness));
    }
    if (settings.roughness != kVoiceDefault) {
        host_->set_param(kRoughness, roughness_to_engine(settings.roughness));
    }
    if (settings.vowel_length != kVoiceDefault) {
        host_->set_param(kVowel, vowel_to_engine(settings.vowel_length));
    }

    // The AV trim has to precede the glottal source it protects.
    const int av_bias = av_bias_to_send(settings);
    if (av_bias != kVoiceDefault) {
        host_->set_param(kAvBias, av_bias);
    }
    if (settings.glottal_source != kVoiceDefault) {
        host_->set_param(kGlottal, settings.glottal_source);
    }
    if (settings.intonation != kVoiceDefault) {
        host_->set_param(kF0Style, settings.intonation);
    }
    if (settings.voicing != kVoiceDefault) {
        host_->set_param(kVoicingMode, settings.voicing);
    }
    if (settings.gender != kVoiceDefault) {
        host_->set_param(kGender, settings.gender);
    }
    const int makeup = makeup_to_send(settings);
    if (makeup != kVoiceDefault) {
        host_->set_param(kMakeup, makeup);
    }
}

bool Synth::speak(const std::wstring& text, const AudioSink& sink,
                  const CancelCheck& keep_going)
{
    if (!host_) {
        return false;
    }
    return host_->speak(text, sink, keep_going);
}

void Synth::stop()
{
    if (host_) {
        host_->stop();
    }
}

void Synth::close()
{
    host_.reset();
    host_rate_ = 0;
    applied_ = false;
}

}  // namespace SoftVoice
