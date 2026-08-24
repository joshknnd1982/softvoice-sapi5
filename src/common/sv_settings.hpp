#pragma once

// Persistent settings for the SoftVoice SAPI 5 interfaces.
//
// A plain INI file under %APPDATA%\SoftVoice SAPI5, deliberately not the
// registry. The engine must be able to speak without reading a registry key,
// and a per-user file also means the configuration utility needs no
// elevation to save. (The registry is still used for COM registration at
// install time - that is SAPI's own discovery mechanism and there is no way
// around it.)
//
// Every value is stored per voice, keyed by the voice's SAPI name, so the
// twenty personalities can be tuned independently in each language.
//
// SettingsWatcher re-reads the file whenever its timestamp or size moves,
// which is what makes a change in the utility take effect on the next thing
// spoken without restarting anything.

#include <map>
#include <string>
#include <vector>
#include <windows.h>

#include "sv_log.hpp"

namespace SoftVoice {

// "leave the personality's own value alone". The engine has no call that
// means "revert", so a preset value can only be preserved by never sending
// anything for it.
//
// This is not a cosmetic default. Every personality is a complete preset,
// and sending a parameter the user never chose replaces that preset's value
// with a guess. Measured with tools\check_clipping.py: forcing SVSetAVBias(0)
// - which looks like the obvious neutral - drives 14 of the 20 voices past
// full scale, and the engine wraps rather than saturating, so it is heard as
// a harsh crackle. Every preset is clean when left alone.
inline constexpr int kVoiceDefault = -1;

// Every tunable the engine exposes, in user units. The engine's own units
// and the mapping onto them live in sv_engine_params.hpp, so the utility and
// the SAPI DLL cannot drift apart.
struct VoiceSettings {
    // Always sent. SAPI applications drive these three directly and expect
    // them to work, and all three have a verified neutral point: pitch 50
    // reproduces the preset exactly, rate 50 is the personality's own
    // measured rate, and SVSetVolume(100) was measured to be a no-op.
    int rate = 50;    // 0..100, 50 = this personality's own speed
    int pitch = 50;   // 0..100, 50 = this personality's own pitch
    int volume = 100; // 0..100, 100 = the preset's own level

    // Only sent when the user has chosen one. There is no known neutral
    // value for these - each preset sets its own - so the sentinel is the
    // only way to keep a personality sounding like itself.
    int inflection = kVoiceDefault;     // 0..100 -> SVSetF0Range 0..500
    int breathiness = kVoiceDefault;    // 0..100 -> SVSetAHBias -60..+20
    int roughness = kVoiceDefault;      // 0..100 -> SVSetF0Perturb 0..500
    int vowel_length = kVoiceDefault;   // 0..100 -> SVSetVowelFactor 20..300

    int glottal_source = kVoiceDefault; // SVSetGlottalSource 0, 2..8
    int intonation = kVoiceDefault;     // SVSetF0Style 0, 2, 4
    int voicing = kVoiceDefault;        // SVSetVoicingMode 0..2
    int gender = kVoiceDefault;         // SVSetGender 1..3

    int av_bias = kVoiceDefault;        // SVSetAVBias -60..0
    int volume_makeup = kVoiceDefault;  // 100..600 percent

    void clamp() noexcept;
};

struct Settings {
    std::wstring default_voice;        // SAPI voice name, may be empty
    unsigned sample_rate = 22050;      // 8000, 11025 or 22050
    log::Level log_level = log::Level::Info;
    std::map<std::wstring, VoiceSettings> voices;  // keyed by SAPI voice name

    // Settings for `voice_name`, falling back to defaults for a voice the
    // file has never seen. Never fails: an unknown voice must still speak.
    [[nodiscard]] VoiceSettings for_voice(const std::wstring& voice_name) const;
    void set_for_voice(const std::wstring& voice_name,
                       const VoiceSettings& value);
};

// The three formats the host can be opened with. Changing this restarts the
// host, because the engine fixes its format at SVOpenSpeech.
inline constexpr unsigned kSampleRates[] = {8000, 11025, 22050};
inline constexpr std::size_t kSampleRateCount = 3;

[[nodiscard]] bool load_settings(const std::wstring& path, Settings* out);
[[nodiscard]] bool save_settings(const std::wstring& path, const Settings& in);

// Watches the settings file and hands out the current values.
//
// The SAPI engine holds one of these and calls refresh() at the top of every
// utterance; it stats the file and only re-parses when something moved, so
// the common case costs one GetFileAttributesEx.
class SettingsWatcher {
public:
    SettingsWatcher();
    ~SettingsWatcher();

    SettingsWatcher(const SettingsWatcher&) = delete;
    SettingsWatcher& operator=(const SettingsWatcher&) = delete;

    // True when the file had changed and the values were reloaded.
    bool refresh();

    [[nodiscard]] Settings snapshot();

    [[nodiscard]] const std::wstring& path() const noexcept { return path_; }

private:
    CRITICAL_SECTION lock_{};
    std::wstring path_;
    Settings settings_;
    FILETIME stamp_{};
    ULONGLONG size_ = 0;
    bool loaded_ = false;
};

}  // namespace SoftVoice
