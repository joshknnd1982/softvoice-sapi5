#include "sv_voices.hpp"

#include <windows.h>

#include "sv_paths.hpp"

namespace SoftVoice {

int voice_id_from_name(const std::wstring& name)
{
    for (std::size_t i = 0; i < kVoiceCount; ++i) {
        if (_wcsicmp(Voice(i).name().c_str(), name.c_str()) == 0) {
            return static_cast<int>(i);
        }
    }
    // An application that stored just the personality still resolves, so a
    // configuration saved before the Spanish voices existed keeps working.
    for (std::size_t i = 0; i < kPersonalityCount; ++i) {
        if (_wcsicmp(kPersonalities[i].name, name.c_str()) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool spanish_installed()
{
    return paths::file_exists(paths::engine_dir() + L"\\Svspan32.dll");
}

std::vector<Voice> available_voices()
{
    const bool spanish = spanish_installed();

    std::vector<Voice> voices;
    voices.reserve(kVoiceCount);
    for (std::size_t i = 0; i < kVoiceCount; ++i) {
        const Voice voice(i);
        if (!spanish && voice.language().bit != 0x1) {
            continue;
        }
        voices.push_back(voice);
    }
    return voices;
}

}  // namespace SoftVoice
