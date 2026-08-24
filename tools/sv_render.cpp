// sv_render - render SoftVoice speech to a WAV file, without SAPI.
//
// This is the tool that demonstrates the engine working on its own terms:
// no SAPI 4, no SAPI 5, no COM registration and no registry. It drives
// svwebspeak-host.exe directly, exactly as the SAPI 5 interface does.
//
//   sv_render --list
//   sv_render --voice "SoftVoice Female" --text "Hello." --out hello.wav
//   sv_render --all --out-dir samples
//
// Every tunable in the settings file can be overridden on the command line,
// so a parameter can be checked by ear before it is saved.

#include <cstdio>
#include <cwctype>
#include <cstdlib>
#include <string>
#include <vector>
#include <windows.h>

#include "sv_engine_params.hpp"
#include "sv_log.hpp"
#include "sv_paths.hpp"
#include "sv_settings.hpp"
#include "sv_synth.hpp"
#include "sv_voices.hpp"

using namespace SoftVoice;

namespace {

void usage()
{
    wprintf(
        L"sv_render - render SoftVoice speech to a WAV file, without SAPI\n"
        L"\n"
        L"  sv_render --list\n"
        L"  sv_render [options] --text TEXT --out FILE.wav\n"
        L"  sv_render [options] --all --out-dir DIR\n"
        L"\n"
        L"Voice selection:\n"
        L"  --voice NAME-OR-ID   a name from --list, or 0 to %u\n"
        L"  --all                every voice, one file each\n"
        L"\n"
        L"Output:\n"
        L"  --out FILE           where to write the WAV\n"
        L"  --out-dir DIR        where to write with --all\n"
        L"  --sample-rate HZ     8000, 11025 or 22050 (default 22050)\n"
        L"\n"
        L"Always applied, 0 to 100:\n"
        L"  --rate N             50 is this voice's own speed\n"
        L"  --pitch N            50 is this voice's own pitch\n"
        L"  --volume N           100 is the voice's own level\n"
        L"\n"
        L"Applied only if given. Left out, the personality's own preset\n"
        L"stands - which is the default, because forcing a value the preset\n"
        L"did not choose overwrites the voice's character, and for AV bias\n"
        L"drives most of the presets past full scale:\n"
        L"  --inflection N       0 to 100\n"
        L"  --breathiness N      0 to 100\n"
        L"  --roughness N        0 to 100\n"
        L"  --vowel-length N     0 to 100\n"
        L"  --glottal N          0, or 2 to 8\n"
        L"  --intonation N       0 normal, 2 monotone, 4 expressive\n"
        L"  --voicing N          0 normal, 1 soft, 2 whispered\n"
        L"  --gender N           1 male, 2 female, 3 neutral\n"
        L"  --av-bias N          -60 to 0\n"
        L"  --volume-makeup N    100 to 600 percent\n"
        L"\n"
        L"  --log-level LEVEL    off, error, warning, info, debug, trace\n",
        static_cast<unsigned>(kVoiceCount - 1));
}

void list_voices()
{
    const std::wstring spanish = paths::engine_dir() + L"\\Svspan32.dll";
    const bool have_spanish = paths::file_exists(spanish);

    wprintf(L"%-4s %-34s %-9s %-8s %-7s %s\n", L"id", L"name", L"language",
            L"gender", L"age", L"pitch");
    for (std::size_t i = 0; i < kVoiceCount; ++i) {
        const Voice voice(i);
        if (!have_spanish && voice.language().bit != 0x1) {
            continue;
        }
        wprintf(L"%-4zu %-34s %-9s %-8s %-7s %d Hz\n", i,
                voice.name().c_str(), voice.language().english,
                voice.personality().gender, voice.personality().age,
                voice.personality().natural_pitch);
    }
    if (!have_spanish) {
        wprintf(L"\nSvspan32.dll is not installed, so the Spanish voices are "
                L"not listed.\n");
    }
}

bool write_wav(const std::wstring& path, const std::vector<char>& pcm,
               unsigned rate, unsigned bits, unsigned channels)
{
#pragma pack(push, 1)
    struct Header {
        char riff[4] = {'R', 'I', 'F', 'F'};
        unsigned riff_size = 0;
        char wave[4] = {'W', 'A', 'V', 'E'};
        char fmt[4] = {'f', 'm', 't', ' '};
        unsigned fmt_size = 16;
        unsigned short format = 1;
        unsigned short channels = 1;
        unsigned sample_rate = 22050;
        unsigned byte_rate = 44100;
        unsigned short block_align = 2;
        unsigned short bits = 16;
        char data[4] = {'d', 'a', 't', 'a'};
        unsigned data_size = 0;
    } header;
#pragma pack(pop)
    static_assert(sizeof(Header) == 44, "the WAV header must be 44 bytes");

    header.channels = static_cast<unsigned short>(channels);
    header.sample_rate = rate;
    header.bits = static_cast<unsigned short>(bits);
    header.block_align = static_cast<unsigned short>(channels * bits / 8);
    header.byte_rate = rate * header.block_align;
    header.data_size = static_cast<unsigned>(pcm.size());
    header.riff_size = static_cast<unsigned>(36 + pcm.size());

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    bool ok = WriteFile(file, &header, sizeof(header), &written, nullptr) &&
              written == sizeof(header);
    if (ok && !pcm.empty()) {
        ok = WriteFile(file, pcm.data(), static_cast<DWORD>(pcm.size()),
                       &written, nullptr) &&
             written == pcm.size();
    }
    CloseHandle(file);
    return ok;
}

std::wstring sanitise(const std::wstring& text)
{
    std::wstring out;
    for (const wchar_t c : text) {
        out += (iswalnum(c) ? c : L'_');
    }
    while (out.size() > 1 && out.back() == L'_') {
        out.pop_back();
    }
    return out;
}

}  // namespace

int wmain(int argc, wchar_t** argv)
{
    log::init(L"render");

    std::wstring text = L"The quick brown fox jumps over the lazy dog.";
    std::wstring out;
    std::wstring out_dir;
    std::wstring voice_arg;
    unsigned sample_rate = 22050;
    bool all = false;
    VoiceSettings settings;

    auto next = [&](int& i) -> std::wstring {
        if (i + 1 >= argc) {
            wprintf(L"error: %s needs a value\n", argv[i]);
            exit(2);
        }
        return argv[++i];
    };

    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--help" || arg == L"-h" || arg == L"/?") {
            usage();
            return 0;
        } else if (arg == L"--list") {
            list_voices();
            return 0;
        } else if (arg == L"--all") {
            all = true;
        } else if (arg == L"--voice") {
            voice_arg = next(i);
        } else if (arg == L"--text") {
            text = next(i);
        } else if (arg == L"--out") {
            out = next(i);
        } else if (arg == L"--out-dir") {
            out_dir = next(i);
        } else if (arg == L"--sample-rate") {
            sample_rate = static_cast<unsigned>(_wtoi(next(i).c_str()));
        } else if (arg == L"--rate") {
            settings.rate = _wtoi(next(i).c_str());
        } else if (arg == L"--pitch") {
            settings.pitch = _wtoi(next(i).c_str());
        } else if (arg == L"--volume") {
            settings.volume = _wtoi(next(i).c_str());
        } else if (arg == L"--inflection") {
            settings.inflection = _wtoi(next(i).c_str());
        } else if (arg == L"--breathiness") {
            settings.breathiness = _wtoi(next(i).c_str());
        } else if (arg == L"--roughness") {
            settings.roughness = _wtoi(next(i).c_str());
        } else if (arg == L"--vowel-length") {
            settings.vowel_length = _wtoi(next(i).c_str());
        } else if (arg == L"--glottal") {
            settings.glottal_source = _wtoi(next(i).c_str());
        } else if (arg == L"--intonation") {
            settings.intonation = _wtoi(next(i).c_str());
        } else if (arg == L"--voicing") {
            settings.voicing = _wtoi(next(i).c_str());
        } else if (arg == L"--gender") {
            settings.gender = _wtoi(next(i).c_str());
        } else if (arg == L"--av-bias") {
            settings.av_bias = _wtoi(next(i).c_str());
        } else if (arg == L"--volume-makeup") {
            settings.volume_makeup = _wtoi(next(i).c_str());
        } else if (arg == L"--log-level") {
            log::set_level(log::level_from_string(next(i)));
        } else {
            wprintf(L"error: unknown option %s\n", arg.c_str());
            usage();
            return 2;
        }
    }
    settings.clamp();

    std::vector<std::size_t> ids;
    if (all) {
        const bool have_spanish =
            paths::file_exists(paths::engine_dir() + L"\\Svspan32.dll");
        for (std::size_t i = 0; i < kVoiceCount; ++i) {
            if (have_spanish || Voice(i).language().bit == 0x1) {
                ids.push_back(i);
            }
        }
        if (out_dir.empty()) {
            out_dir = L".";
        }
    } else {
        int id = 0;
        if (!voice_arg.empty()) {
            if (iswdigit(voice_arg[0])) {
                id = _wtoi(voice_arg.c_str());
                if (id < 0 || id >= static_cast<int>(kVoiceCount)) {
                    wprintf(L"error: voice id %d is out of range\n", id);
                    return 2;
                }
            } else {
                id = voice_id_from_name(voice_arg);
                if (id < 0) {
                    wprintf(L"error: no voice named \"%s\"; try --list\n",
                            voice_arg.c_str());
                    return 2;
                }
            }
        }
        ids.push_back(static_cast<std::size_t>(id));
        if (out.empty()) {
            out = L"softvoice.wav";
        }
    }

    Synth synth;
    HostError error;
    if (!synth.ensure_host(sample_rate, &error)) {
        wprintf(L"error: %s\n", error.message.c_str());
        return 1;
    }
    wprintf(L"engine ready: %u Hz, %u bit, languages 0x%x\n",
            synth.sample_rate(), synth.bits_per_sample(), synth.languages());

    int failures = 0;
    for (const std::size_t id : ids) {
        const Voice voice(id);
        synth.apply(static_cast<int>(id), settings, SpeechAdjust());

        std::vector<char> pcm;
        const bool ok =
            synth.speak(text, [&](const void* data, std::size_t bytes) {
                const auto* start = static_cast<const char*>(data);
                pcm.insert(pcm.end(), start, start + bytes);
                return true;
            });
        if (!ok) {
            wprintf(L"error: %s failed to render\n", voice.name().c_str());
            ++failures;
            continue;
        }

        std::wstring path = out;
        if (all) {
            wchar_t leaf[256];
            swprintf_s(leaf, L"%s\\%02zu_%s_%s.wav", out_dir.c_str(), id,
                       voice.language().code,
                       sanitise(voice.personality().name).c_str());
            path = leaf;
        }
        if (!write_wav(path, pcm, synth.sample_rate(),
                       synth.bits_per_sample(), 1)) {
            wprintf(L"error: could not write %s\n", path.c_str());
            ++failures;
            continue;
        }
        wprintf(L"%-34s %6.2f s  %s\n", voice.name().c_str(),
                pcm.size() / 2.0 / synth.sample_rate(), path.c_str());
    }

    return failures ? 1 : 0;
}
