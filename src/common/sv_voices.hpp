#pragma once

// The SoftVoice voice catalogue.
//
// The engine ships twenty built-in personalities and two languages, and the
// two are independent: SVSetPersonality selects the voice, SVSetLanguage
// selects the letter-to-sound rules. SAPI has no notion of switching a
// voice's language, so each combination is published as its own voice token
// - twenty English and twenty Spanish, forty in all.
//
// None of this comes from the registry. The catalogue is compiled in and the
// enumerator hands SAPI tokens built from it, so installing writes only the
// COM registration SAPI needs to find the DLL at all.

#include <cstddef>
#include <string>
#include <vector>

namespace SoftVoice {

// A built-in personality, in the engine's own index order. That order is the
// REVERSE of the order the names are stored in SVctl32.DLL; getting it wrong
// makes every voice the wrong voice.
struct Personality {
    const wchar_t* name;
    int index;           // what SVSetPersonality takes
    int natural_pitch;   // the preset's own pitch, in engine units
    int natural_rate;    // the preset's own rate, in engine units
    const wchar_t* gender;
    const wchar_t* age;
};

inline constexpr std::size_t kPersonalityCount = 20;

// natural_pitch is the engine's own value, the 16-bit field at offset 4 of
// the per-voice SVGetVoiceInfo block. Pitch is applied relative to it so a
// voice keeps its character: a wrong value here detunes that voice at every
// slider position, including the neutral one.
//
// natural_rate is the same idea for speed, measured rather than read: the
// engine's duration is exactly inversely proportional to its rate (checked to
// 0.3% - rate x duration came out 424.5 at rate 100 and 423.4 at rate 200),
// so timing a fixed sentence at the preset and again at a known rate gives
// the preset's own value. Regenerate with tools\measure_presets.py.
//
// These vary far more than they look: Fast Fred is 301 and Choir Boy 89, a
// spread of more than three to one. Sending a single "natural" 150 for every
// voice - which is what this wrapper did until the rates were measured -
// makes Fast Fred sedate and Choir Boy hurried, throwing away most of what
// distinguishes the twenty personalities from one another.
inline constexpr Personality kPersonalities[kPersonalityCount] = {
    {L"Male",           0,  90, 150, L"Male",    L"Adult"},
    {L"Female",         1, 200, 150, L"Female",  L"Adult"},
    {L"Large Male",     2,  80, 150, L"Male",    L"Adult"},
    {L"Child",          3, 350, 129, L"Neutral", L"Child"},
    {L"Giant Male",     4,  45, 139, L"Male",    L"Adult"},
    {L"Mellow Female",  5, 190, 139, L"Female",  L"Adult"},
    {L"Mellow Male",    6, 110, 150, L"Male",    L"Adult"},
    {L"Crisp Male",     7, 125, 150, L"Male",    L"Adult"},
    {L"The Fly",        8, 480, 150, L"Neutral", L"Adult"},
    {L"Robotoid",       9,  90, 150, L"Neutral", L"Adult"},
    {L"Martian",       10,  80, 150, L"Neutral", L"Adult"},
    {L"Colossus",      11,  66, 138, L"Male",    L"Adult"},
    {L"Fast Fred",     12, 135, 301, L"Male",    L"Adult"},
    {L"Old Woman",     13, 270, 114, L"Female",  L"Senior"},
    {L"Munchkin",      14,  90, 150, L"Neutral", L"Adult"},
    {L"Troll",         15, 110, 200, L"Male",    L"Adult"},
    {L"Nerd",          16, 140, 153, L"Male",    L"Adult"},
    {L"Milktoast",     17, 120, 163, L"Male",    L"Adult"},
    {L"Tipsy",         18, 145, 114, L"Male",    L"Adult"},
    {L"Choir Boy",     19, 310,  89, L"Male",    L"Child"},
};

// SVSetLanguage takes the engine's own language bit, and only accepts one
// that was loaded at SVOpenSpeech. The host opens with English|Spanish, so
// whichever rule DLLs are present next to it become selectable; Spanish
// needs Svspan32.dll.
struct Language {
    const wchar_t* code;    // "en" / "es"
    int bit;                // what SVSetLanguage takes
    const wchar_t* lcid;    // SAPI Language attribute, hex, no 0x
    const wchar_t* english; // display name in English
    const wchar_t* suffix;  // appended to the voice name
};

inline constexpr std::size_t kLanguageCount = 2;

inline constexpr Language kLanguages[kLanguageCount] = {
    {L"en", 0x1, L"409", L"English", L""},
    {L"es", 0x2, L"c0a", L"Spanish", L" (Spanish)"},
};

inline constexpr std::size_t kVoiceCount = kPersonalityCount * kLanguageCount;

// One publishable SAPI voice: a personality rendered in one language.
class Voice {
public:
    explicit Voice(std::size_t id = 0) noexcept
        : id_(id < kVoiceCount ? id : 0)
    {
    }

    [[nodiscard]] std::size_t id() const noexcept { return id_; }

    [[nodiscard]] const Personality& personality() const noexcept
    {
        return kPersonalities[id_ % kPersonalityCount];
    }

    [[nodiscard]] const Language& language() const noexcept
    {
        return kLanguages[id_ / kPersonalityCount];
    }

    // "SoftVoice Male", "SoftVoice Male (Spanish)". Unique across the
    // catalogue, which is what lets a token be resolved back to an id.
    [[nodiscard]] std::wstring name() const
    {
        return std::wstring(L"SoftVoice ") + personality().name +
               language().suffix;
    }

    // What a person hears when they ask the utility to describe the voice.
    [[nodiscard]] std::wstring description() const
    {
        return std::wstring(personality().name) + L" - " +
               language().english + L", " + personality().gender;
    }

private:
    std::size_t id_;
};

// The id whose name matches, or -1. Comparison is case-insensitive because
// SAPI applications round-trip the name through their own storage.
[[nodiscard]] int voice_id_from_name(const std::wstring& name);

// True when Svspan32.dll is installed beside the engine, which is what makes
// the engine load Spanish at all.
[[nodiscard]] bool spanish_installed();

// The voices that can actually speak on this machine: all twenty
// personalities in English, plus the same twenty in Spanish when
// Svspan32.dll is present.
//
// Decided by looking for that file rather than by asking the engine. The
// SAPI enumerator runs every time an application lists voices, and starting
// a host to answer it would make opening a speech settings dialog take
// seconds; the engine consults the same file either way.
[[nodiscard]] std::vector<Voice> available_voices();

}  // namespace SoftVoice
