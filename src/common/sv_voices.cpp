#include "sv_voices.hpp"

#include <windows.h>

#include "sv_log.hpp"
#include "sv_paths.hpp"

namespace SoftVoice {

namespace {

std::wstring trimmed(const std::wstring& text)
{
    const std::size_t first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        return std::wstring();
    }
    const std::size_t last = text.find_last_not_of(L" \t\r\n");
    return text.substr(first, last - first + 1);
}

// The install-time voice selection, as read from voices.ini.
//
// What is kept is the set of keys switched off, because absence is the
// interesting state: a key that is missing, a file that is missing entirely
// and a file that cannot be read all have to leave a voice published.
//
// The file is parsed here rather than handed to GetPrivateProfileString for
// the reason given over paths::read_text_file - the profile API cannot see
// past the byte order mark an editor is apt to leave behind, and this file is
// documented as one people may edit.
class Selection {
public:
    static Selection load(HMODULE module)
    {
        Selection selection;

        std::wstring text;
        if (!paths::read_text_file(selection_path(module), &text)) {
            return selection;
        }

        bool in_voices = false;
        std::size_t line_start = 0;
        while (line_start <= text.size()) {
            std::size_t line_end = text.find(L'\n', line_start);
            if (line_end == std::wstring::npos) {
                line_end = text.size();
            }
            const std::wstring line =
                trimmed(text.substr(line_start, line_end - line_start));
            line_start = line_end + 1;

            if (line.empty() || line[0] == L';' || line[0] == L'#') {
                continue;
            }
            if (line[0] == L'[') {
                const std::size_t close = line.find(L']');
                const std::wstring section =
                    close == std::wstring::npos
                        ? std::wstring()
                        : trimmed(line.substr(1, close - 1));
                in_voices = _wcsicmp(section.c_str(), L"Voices") == 0;
                continue;
            }
            if (!in_voices) {
                continue;
            }

            const std::size_t equals = line.find(L'=');
            if (equals == std::wstring::npos) {
                continue;
            }
            const std::wstring key = trimmed(line.substr(0, equals));
            const std::wstring value = trimmed(line.substr(equals + 1));
            if (key.empty()) {
                continue;
            }
            selection.present_ = true;
            if (value == L"0") {
                selection.excluded_.push_back(key);
            }
        }
        return selection;
    }

    [[nodiscard]] bool publishes(const Voice& voice) const
    {
        if (!present_) {
            return true;
        }
        const std::wstring key = selection_key(voice);
        for (const std::wstring& excluded : excluded_) {
            if (_wcsicmp(excluded.c_str(), key.c_str()) == 0) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool present() const noexcept { return present_; }

private:
    std::vector<std::wstring> excluded_;
    bool present_ = false;
};

}  // namespace

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

std::wstring selection_key(const Voice& voice)
{
    return std::wstring(voice.language().code) + L"." +
           voice.personality().name;
}

std::wstring selection_path(HMODULE module)
{
    return paths::engine_dir(module) + L"\\voices.ini";
}

std::vector<Voice> available_voices()
{
    const bool spanish = spanish_installed();
    const Selection selection = Selection::load(nullptr);

    std::vector<Voice> speakable;  // what the engine could say here
    std::vector<Voice> published;  // what this installation chose to offer
    speakable.reserve(kVoiceCount);
    published.reserve(kVoiceCount);
    for (std::size_t i = 0; i < kVoiceCount; ++i) {
        const Voice voice(i);
        if (!spanish && voice.language().bit != 0x1) {
            continue;
        }
        speakable.push_back(voice);
        if (selection.publishes(voice)) {
            published.push_back(voice);
        }
    }

    // A selection that leaves nothing is a damaged selection, not a choice.
    // The installer refuses to write one, so getting here means the file has
    // been edited into a state that would silence the computer; publishing
    // everything instead at least leaves a voice to explain the problem with.
    if (published.empty() && !speakable.empty()) {
        SV_LOG_ERROR(
            "voices: the selection file hides every voice; ignoring it");
        return speakable;
    }

    if (selection.present()) {
        SV_LOG_DEBUG("voices: %zu of %zu selected", published.size(),
                     speakable.size());
    }
    return published;
}

}  // namespace SoftVoice
