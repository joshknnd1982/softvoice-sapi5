#pragma once

// The mapping from the settings' 0-100 user units onto the engine's own.
//
// Shared by the SAPI DLL and the configuration utility so the two cannot
// drift: what the utility previews is byte-for-byte what the engine will
// render. Every bound here was established by probing the engine, not by
// reading a manual - the return code 7010 means "out of range", and the
// wraparound limits were found by rendering and counting samples that jump
// from near +full scale to near -full in a single step.

#include <cstddef>

#include "sv_settings.hpp"
#include "sv_voices.hpp"

namespace SoftVoice {
namespace params {

// The host's parameter numbers, as it decodes them from a CMD_PARAM frame.
enum Param : unsigned short {
    kRate = 1,
    kPitch = 2,
    kVolume = 3,
    kPersonality = 4,
    kInflection = 5,
    kLanguage = 6,
    kGlottal = 7,
    kF0Style = 8,
    kVoicingMode = 9,
    kBreath = 10,
    kRoughness = 11,
    kVowel = 12,
    kGender = 13,
    kAvBias = 14,
    kMakeup = 15,
};

// SVSetRate accepts 20..500 and the engine's own natural speed is 150.
inline constexpr int kRateMin = 20;
inline constexpr int kRateNatural = 150;
inline constexpr int kRateMax = 500;

inline constexpr int kPitchMin = 10;
inline constexpr int kPitchMax = 2000;

// SVSetF0Range accepts 0..500. Capping it lower puts more than half the
// engine's expressive range out of reach.
inline constexpr int kInflectionMax = 500;

// SVSetAHBias is clean to +20 and wraps at +25.
inline constexpr int kBreathMin = -60;
inline constexpr int kBreathMax = 20;

inline constexpr int kRoughnessMax = 500;

// SVSetVowelFactor, where 100 is natural.
inline constexpr int kVowelMin = 20;
inline constexpr int kVowelNatural = 100;
inline constexpr int kVowelMax = 300;

// Glottal sources 2..8 all overflow at the default voicing amplitude - 364
// to 4166 wraparounds in a single test phrase. Trimming AV bias to -20 makes
// every one of them clean, so the trim travels with the source rather than
// being left as a trap.
inline constexpr int kGlottalAvTrim = -20;

// Whispering really is quieter, but the engine renders it 19 dB below
// normal, which is not usable as a screen reader voice. Its peak only
// reaches 4448 of 32767, so there is ample headroom to lift it back.
inline constexpr int kWhisperMakeup = 400;
inline constexpr int kVoicingWhispered = 2;

// Piecewise, and relative to the selected personality: 50% is that voice's
// own speed, so Fast Fred stays fast and Choir Boy stays slow. Anchoring
// every voice on a single 150 instead flattens a better than three-to-one
// spread of preset rates onto one speed.
[[nodiscard]] inline int rate_to_engine(int percent, int natural) noexcept
{
    const int scaled =
        percent <= 50
            ? kRateMin + percent * (natural - kRateMin) / 50
            : natural + (percent - 50) * (kRateMax - natural) / 50;
    return scaled < kRateMin ? kRateMin
                             : (scaled > kRateMax ? kRateMax : scaled);
}

// Relative to the selected personality: 50% is that voice's own pitch, so
// Child stays high and Colossus stays deep. At 50% this must be exactly the
// preset value or the neutral position would detune every voice.
[[nodiscard]] inline int pitch_to_engine(int percent, int natural) noexcept
{
    const int scaled =
        static_cast<int>(natural * (0.5 + static_cast<double>(percent) / 100.0));
    return scaled < kPitchMin ? kPitchMin
                              : (scaled > kPitchMax ? kPitchMax : scaled);
}

[[nodiscard]] inline int inflection_to_engine(int percent) noexcept
{
    return percent * kInflectionMax / 100;
}

[[nodiscard]] inline int breath_to_engine(int percent) noexcept
{
    return kBreathMin + (kBreathMax - kBreathMin) * percent / 100;
}

[[nodiscard]] inline int roughness_to_engine(int percent) noexcept
{
    return kRoughnessMax * percent / 100;
}

// 50% is the engine's natural 100; the ends are 20 and 300.
[[nodiscard]] inline int vowel_to_engine(int percent) noexcept
{
    if (percent <= 50) {
        return kVowelMin + (kVowelNatural - kVowelMin) * percent / 50;
    }
    return kVowelNatural + (kVowelMax - kVowelNatural) * (percent - 50) / 50;
}

// The AV bias to send, or kVoiceDefault to send nothing at all.
//
// Sending nothing is the normal case and it matters: SVSetAVBias(0) looks
// like a harmless neutral and is not. Measured with tools\check_clipping.py,
// forcing 0 makes 14 of the 20 presets wrap past full scale - up to 2096
// wraparounds in one sentence on Colossus and Crisp Male - while every
// preset left alone is completely clean. A bias is only ever sent when the
// user asked for one, or to protect a glottal source they chose.
[[nodiscard]] inline int av_bias_to_send(const VoiceSettings& s) noexcept
{
    if (s.av_bias != kVoiceDefault) {
        return s.av_bias;
    }
    // Sources 2..8 overflow the engine's fixed-point mixer at the default
    // voicing amplitude, so the trim travels with the source rather than
    // being left as a trap for whoever selects one.
    if (s.glottal_source != kVoiceDefault && s.glottal_source != 0) {
        return kGlottalAvTrim;
    }
    return kVoiceDefault;
}

// The volume makeup to send, or kVoiceDefault to send nothing.
[[nodiscard]] inline int makeup_to_send(const VoiceSettings& s) noexcept
{
    if (s.volume_makeup != kVoiceDefault) {
        return s.volume_makeup;
    }
    // Whispering is real but the engine renders it 19 dB down, which is not
    // usable as a screen reader voice, so choosing it implies the lift.
    if (s.voicing == kVoicingWhispered) {
        return kWhisperMakeup;
    }
    return kVoiceDefault;
}

// ---------------------------------------------------------------- choices

struct Choice {
    int value;
    const wchar_t* label;
};

// Every enumerated list starts with the sentinel that sends nothing, so a
// personality keeps its own setting until it is deliberately overridden. The
// presets really do differ - Robotoid, Martian and Colossus use glottal
// source 2 and Tipsy and Choir Boy use 4 - so sending a fixed value would
// flatten them.
inline constexpr Choice kGlottalChoices[] = {
    {kVoiceDefault, L"Voice default"},
    {0, L"Standard"},
    {2, L"Soft"},
    {3, L"Rounded"},
    {4, L"Open"},
    {5, L"Relaxed"},
    {6, L"Bright"},
    {7, L"Buzzy"},
    {8, L"Harsh"},
};
inline constexpr std::size_t kGlottalChoiceCount = 9;

// Named from measured pitch spread over a test phrase. Male / Female
// 10th-to-90th percentile F0 in Hz: style 0 is 54.7 / 96.9, style 2 is
// 3.7 / 5.4, style 4 is 67.3 / 121.9.
inline constexpr Choice kIntonationChoices[] = {
    {kVoiceDefault, L"Voice default"},
    {0, L"Normal"},
    {2, L"Monotone"},
    {4, L"Expressive"},
};
inline constexpr std::size_t kIntonationChoiceCount = 4;

inline constexpr Choice kVoicingChoices[] = {
    {kVoiceDefault, L"Voice default"},
    {0, L"Normal"},
    {1, L"Soft"},
    {2, L"Whispered"},
};
inline constexpr std::size_t kVoicingChoiceCount = 4;

inline constexpr Choice kGenderChoices[] = {
    {kVoiceDefault, L"Voice default"},
    {1, L"Male"},
    {2, L"Female"},
    {3, L"Neutral"},
};
inline constexpr std::size_t kGenderChoiceCount = 4;

}  // namespace params
}  // namespace SoftVoice
