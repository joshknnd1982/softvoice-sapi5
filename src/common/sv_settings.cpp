#include "sv_settings.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "sv_paths.hpp"

namespace SoftVoice {

namespace {

constexpr wchar_t kGeneralSection[] = L"general";
constexpr wchar_t kVoicePrefix[] = L"voice:";

int clamp_int(int value, int low, int high) noexcept
{
    return value < low ? low : (value > high ? high : value);
}

// An enumerated setting keeps kVoiceDefault, or one of its allowed values;
// anything else in a hand-edited file falls back to the voice's own.
int clamp_choice(int value, const int* allowed, std::size_t count) noexcept
{
    if (value == kVoiceDefault) {
        return kVoiceDefault;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (allowed[i] == value) {
            return value;
        }
    }
    return kVoiceDefault;
}

std::wstring trim(const std::wstring& text)
{
    std::size_t first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        return std::wstring();
    }
    std::size_t last = text.find_last_not_of(L" \t\r\n");
    return text.substr(first, last - first + 1);
}

// Read a whole file as UTF-8 and widen it. The settings file is written by
// this code, but a person may well edit it by hand, so a BOM is tolerated.
bool read_text_file(const std::wstring& path, std::wstring* out)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart > (16 << 20)) {
        CloseHandle(file);
        return false;
    }
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const bool ok = bytes.empty() ||
                    (ReadFile(file, bytes.data(),
                              static_cast<DWORD>(bytes.size()), &read,
                              nullptr) &&
                     read == bytes.size());
    CloseHandle(file);
    if (!ok) {
        return false;
    }
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bytes.erase(0, 3);
    }
    if (bytes.empty()) {
        out->clear();
        return true;
    }
    const int needed = MultiByteToWideChar(
        CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    out->assign(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(),
                        static_cast<int>(bytes.size()), out->data(), needed);
    return true;
}

bool write_text_file(const std::wstring& path, const std::wstring& text)
{
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    std::string bytes(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                        static_cast<int>(text.size()), bytes.data(), needed,
                        nullptr, nullptr);

    // Write to a sibling then rename, so a reader never sees half a file and
    // a crash mid-save cannot lose the settings that were already there.
    const std::wstring temp = path + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const bool ok = WriteFile(file, bytes.data(),
                              static_cast<DWORD>(bytes.size()), &written,
                              nullptr) &&
                    written == bytes.size();
    FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok) {
        DeleteFileW(temp.c_str());
        return false;
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}

void apply_pair(VoiceSettings* voice, const std::wstring& key,
                const std::wstring& value)
{
    const int number = _wtoi(value.c_str());
    if (key == L"rate") voice->rate = number;
    else if (key == L"pitch") voice->pitch = number;
    else if (key == L"volume") voice->volume = number;
    else if (key == L"inflection") voice->inflection = number;
    else if (key == L"breathiness") voice->breathiness = number;
    else if (key == L"roughness") voice->roughness = number;
    else if (key == L"vowel_length") voice->vowel_length = number;
    else if (key == L"glottal_source") voice->glottal_source = number;
    else if (key == L"intonation") voice->intonation = number;
    else if (key == L"voicing") voice->voicing = number;
    else if (key == L"gender") voice->gender = number;
    else if (key == L"av_bias") voice->av_bias = number;
    else if (key == L"volume_makeup") voice->volume_makeup = number;
}

void append_voice(std::wstring* out, const std::wstring& name,
                  const VoiceSettings& voice)
{
    wchar_t line[256];
    *out += L"\r\n[";
    *out += kVoicePrefix;
    *out += name;
    *out += L"]\r\n";
    swprintf_s(line, L"rate=%d\r\n", voice.rate); *out += line;
    swprintf_s(line, L"pitch=%d\r\n", voice.pitch); *out += line;
    swprintf_s(line, L"volume=%d\r\n", voice.volume); *out += line;
    swprintf_s(line, L"inflection=%d\r\n", voice.inflection); *out += line;
    swprintf_s(line, L"breathiness=%d\r\n", voice.breathiness); *out += line;
    swprintf_s(line, L"roughness=%d\r\n", voice.roughness); *out += line;
    swprintf_s(line, L"vowel_length=%d\r\n", voice.vowel_length); *out += line;
    swprintf_s(line, L"glottal_source=%d\r\n", voice.glottal_source); *out += line;
    swprintf_s(line, L"intonation=%d\r\n", voice.intonation); *out += line;
    swprintf_s(line, L"voicing=%d\r\n", voice.voicing); *out += line;
    swprintf_s(line, L"gender=%d\r\n", voice.gender); *out += line;
    swprintf_s(line, L"av_bias=%d\r\n", voice.av_bias); *out += line;
    swprintf_s(line, L"volume_makeup=%d\r\n", voice.volume_makeup); *out += line;
}

}  // namespace

namespace {

// Clamp a percentage that is allowed to be "not set".
int clamp_optional_percent(int value) noexcept
{
    return value == kVoiceDefault ? kVoiceDefault : clamp_int(value, 0, 100);
}

}  // namespace

void VoiceSettings::clamp() noexcept
{
    rate = clamp_int(rate, 0, 100);
    pitch = clamp_int(pitch, 0, 100);
    volume = clamp_int(volume, 0, 100);

    inflection = clamp_optional_percent(inflection);
    breathiness = clamp_optional_percent(breathiness);
    roughness = clamp_optional_percent(roughness);
    vowel_length = clamp_optional_percent(vowel_length);

    // Glottal source 1 renders identically to 0 and is left out. Sources
    // 2..8 all overflow the engine's fixed-point mixer at the default
    // voicing amplitude, which is what the AV trim below exists for.
    static constexpr int kGlottal[] = {0, 2, 3, 4, 5, 6, 7, 8};
    glottal_source = clamp_choice(glottal_source, kGlottal, 8);

    // F0 style 1 collapses voicing to a barely audible whisper and style 3
    // is audibly identical to style 2, so neither is offered.
    static constexpr int kIntonation[] = {0, 2, 4};
    intonation = clamp_choice(intonation, kIntonation, 3);

    static constexpr int kVoicing[] = {0, 1, 2};
    voicing = clamp_choice(voicing, kVoicing, 3);

    static constexpr int kGender[] = {1, 2, 3};
    gender = clamp_choice(gender, kGender, 3);

    // SVSetAVBias is clean to 0 and wraps at +5, and never wraps downward,
    // so only the safe span is accepted. -1 is the "not set" sentinel rather
    // than a bias of minus one; nothing offers -1 as a choice, and the
    // difference between -1 and 0 is inaudible in any case.
    if (av_bias != kVoiceDefault) {
        av_bias = clamp_int(av_bias, -60, 0);
    }
    if (volume_makeup != kVoiceDefault) {
        volume_makeup = clamp_int(volume_makeup, 100, 600);
    }
}

VoiceSettings Settings::for_voice(const std::wstring& voice_name) const
{
    const auto it = voices.find(voice_name);
    if (it == voices.end()) {
        return VoiceSettings();
    }
    VoiceSettings result = it->second;
    result.clamp();
    return result;
}

void Settings::set_for_voice(const std::wstring& voice_name,
                             const VoiceSettings& value)
{
    VoiceSettings copy = value;
    copy.clamp();
    voices[voice_name] = copy;
}

bool load_settings(const std::wstring& path, Settings* out)
{
    if (!out) {
        return false;
    }
    *out = Settings();

    std::wstring text;
    if (!read_text_file(path, &text)) {
        return false;
    }

    std::wstring section;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t end = text.find(L'\n', pos);
        if (end == std::wstring::npos) {
            end = text.size();
        }
        const std::wstring line = trim(text.substr(pos, end - pos));
        pos = end + 1;

        if (line.empty() || line[0] == L';' || line[0] == L'#') {
            continue;
        }
        if (line.front() == L'[' && line.back() == L']') {
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }
        const std::size_t equals = line.find(L'=');
        if (equals == std::wstring::npos) {
            continue;
        }
        const std::wstring key = trim(line.substr(0, equals));
        const std::wstring value = trim(line.substr(equals + 1));

        if (_wcsicmp(section.c_str(), kGeneralSection) == 0) {
            if (key == L"default_voice") {
                out->default_voice = value;
            } else if (key == L"sample_rate") {
                out->sample_rate =
                    static_cast<unsigned>(_wtoi(value.c_str()));
            } else if (key == L"log_level") {
                out->log_level = log::level_from_string(value);
            }
            continue;
        }
        if (_wcsnicmp(section.c_str(), kVoicePrefix, wcslen(kVoicePrefix)) == 0) {
            const std::wstring name = section.substr(wcslen(kVoicePrefix));
            apply_pair(&out->voices[name], key, value);
        }
    }

    bool valid_rate = false;
    for (std::size_t i = 0; i < kSampleRateCount; ++i) {
        valid_rate = valid_rate || out->sample_rate == kSampleRates[i];
    }
    if (!valid_rate) {
        out->sample_rate = 22050;
    }
    for (auto& [name, voice] : out->voices) {
        (void)name;
        voice.clamp();
    }
    return true;
}

bool save_settings(const std::wstring& path, const Settings& in)
{
    std::wstring text;
    text.reserve(4096);
    text +=
        L"; SoftVoice SAPI 5 settings.\r\n"
        L";\r\n"
        L"; Written by the SoftVoice configuration utility. Safe to edit by\r\n"
        L"; hand: the speech engine re-reads this file whenever it changes,\r\n"
        L"; so a saved change is heard on the next thing spoken.\r\n"
        L";\r\n"
        L"; rate, pitch and volume are 0 to 100. For rate and pitch, 50 means\r\n"
        L"; this personality's own speed and pitch, so the voice keeps its\r\n"
        L"; character; volume 100 is the preset's own level.\r\n"
        L";\r\n"
        L"; Everything else is -1 for \"leave the personality's own setting\r\n"
        L"; alone\", which is the default. That is not just tidiness: each of\r\n"
        L"; the twenty personalities is a complete preset, and forcing a value\r\n"
        L"; the preset did not choose can push the engine past full scale,\r\n"
        L"; which it wraps rather than clips and which is heard as a crackle.\r\n"
        L"; Set one only if you want to override that voice.\r\n"
        L"\r\n[";
    text += kGeneralSection;
    text += L"]\r\n";
    text += L"default_voice=" + in.default_voice + L"\r\n";
    wchar_t line[128];
    swprintf_s(line, L"sample_rate=%u\r\n", in.sample_rate);
    text += line;
    text += std::wstring(L"log_level=") + log::level_to_string(in.log_level) +
            L"\r\n";

    for (const auto& [name, voice] : in.voices) {
        append_voice(&text, name, voice);
    }
    return write_text_file(path, text);
}

SettingsWatcher::SettingsWatcher()
    : path_(paths::settings_file())
{
    InitializeCriticalSection(&lock_);
}

SettingsWatcher::~SettingsWatcher()
{
    DeleteCriticalSection(&lock_);
}

bool SettingsWatcher::refresh()
{
    WIN32_FILE_ATTRIBUTE_DATA info = {};
    const bool present =
        GetFileAttributesExW(path_.c_str(), GetFileExInfoStandard, &info) != 0;

    EnterCriticalSection(&lock_);
    if (!present) {
        // No file yet: the built-in defaults are correct, and saying so once
        // is more useful than reporting a change on every utterance.
        const bool first = !loaded_;
        loaded_ = true;
        stamp_ = FILETIME{};
        size_ = 0;
        settings_ = Settings();
        LeaveCriticalSection(&lock_);
        if (first) {
            SV_LOG_INFO("settings: no file at %S, using built-in defaults",
                        path_.c_str());
        }
        return first;
    }

    const ULONGLONG size =
        (static_cast<ULONGLONG>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    const bool unchanged =
        loaded_ && size == size_ &&
        CompareFileTime(&info.ftLastWriteTime, &stamp_) == 0;
    if (unchanged) {
        LeaveCriticalSection(&lock_);
        return false;
    }

    Settings loaded;
    const bool ok = load_settings(path_, &loaded);
    if (ok) {
        settings_ = loaded;
        stamp_ = info.ftLastWriteTime;
        size_ = size;
        loaded_ = true;
    }
    LeaveCriticalSection(&lock_);

    if (ok) {
        log::set_level(loaded.log_level);
        SV_LOG_INFO("settings: reloaded %S (%llu bytes, %u voices tuned, "
                    "log level %S)",
                    path_.c_str(), size,
                    static_cast<unsigned>(loaded.voices.size()),
                    log::level_to_string(loaded.log_level));
    } else {
        SV_LOG_WARN("settings: could not read %S; keeping previous values",
                    path_.c_str());
    }
    return ok;
}

Settings SettingsWatcher::snapshot()
{
    EnterCriticalSection(&lock_);
    Settings copy = settings_;
    LeaveCriticalSection(&lock_);
    return copy;
}

}  // namespace SoftVoice
