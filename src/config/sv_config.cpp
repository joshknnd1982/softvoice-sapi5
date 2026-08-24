// The SoftVoice configuration utility.
//
// A plain Win32 dialog, deliberately: standard controls are what screen
// readers read natively and what keyboard navigation already works for.
// Nothing is owner-drawn, nothing is disabled, and every control is in the
// tab order with a label of its own.
//
// Two things the utility promises:
//
//  * A change takes effect immediately. Every edit is written to the
//    settings file as soon as it is made, and the SAPI 5 engine re-reads
//    that file at the top of every utterance, so the next thing any
//    application speaks already uses the new value. Nothing needs restarting.
//  * A change is permanent. The same write is the save, so closing the
//    utility - by any route, including the window's close button - cannot
//    lose it.
//
// Preview rendering happens on its own thread. Rendering a sentence takes a
// noticeable fraction of a second, and blocking the message loop for that
// would freeze the dialog under the very screen reader the user is
// listening to.

// windows.h first: commctrl.h, mmsystem.h and shellapi.h all build on its
// types and do not include it themselves.
#include <windows.h>

#include <commctrl.h>
#include <mmsystem.h>
#include <shellapi.h>

#include <cstring>
#include <string>
#include <vector>

#include "sv_config_ids.h"
#include "sv_engine_params.hpp"
#include "sv_log.hpp"
#include "sv_paths.hpp"
#include "sv_settings.hpp"
#include "sv_synth.hpp"
#include "sv_voices.hpp"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")

using namespace SoftVoice;

namespace {

// ------------------------------------------------------------- the model ---

Settings g_settings;
std::wstring g_settings_path;
std::vector<Voice> g_voices;
int g_current = 0;
bool g_loading = false;  // suppresses the change handlers while repopulating
HWND g_dialog = nullptr;

// The four character settings. Their first entry is what every voice starts
// on: send nothing and let the personality's own preset stand. A number is
// only sent once one is deliberately chosen here.
constexpr params::Choice kPercentChoices[] = {
    {kVoiceDefault, L"The voice's own"},
    {0, L"0"},   {10, L"10"}, {20, L"20"}, {30, L"30"}, {40, L"40"},
    {50, L"50"}, {60, L"60"}, {70, L"70"}, {80, L"80"}, {90, L"90"},
    {100, L"100"},
};
constexpr std::size_t kPercentChoiceCount = 12;

// Voicing amplitude. Only the downward span is offered: the engine is clean
// to 0 and wraps at +5, and it never wraps downward.
constexpr params::Choice kAvBiasChoices[] = {
    {kVoiceDefault, L"The voice's own"}, {0, L"Full (0)"},
    {-10, L"-10"},                       {-20, L"Trimmed (-20)"},
    {-30, L"-30"},                       {-40, L"-40"},
    {-50, L"-50"},                       {-60, L"Quietest (-60)"},
};
constexpr std::size_t kAvBiasChoiceCount = 8;

constexpr params::Choice kMakeupChoices[] = {
    {kVoiceDefault, L"Automatic"}, {100, L"100 percent"},
    {150, L"150 percent"},         {200, L"200 percent"},
    {300, L"300 percent"},         {400, L"400 percent"},
    {500, L"500 percent"},         {600, L"600 percent"},
};
constexpr std::size_t kMakeupChoiceCount = 8;

constexpr params::Choice kLogChoices[] = {
    {static_cast<int>(log::Level::Off), L"Off"},
    {static_cast<int>(log::Level::Error), L"Errors only"},
    {static_cast<int>(log::Level::Warning), L"Warnings"},
    {static_cast<int>(log::Level::Info), L"Normal"},
    {static_cast<int>(log::Level::Debug), L"Detailed"},
    {static_cast<int>(log::Level::Trace), L"Everything"},
};
constexpr std::size_t kLogChoiceCount = 6;

// ------------------------------------------------------ the preview thread --

CRITICAL_SECTION g_preview_lock;
HANDLE g_preview_event = nullptr;
HANDLE g_preview_thread = nullptr;
volatile LONG g_preview_quit = 0;

struct PreviewRequest {
    bool pending = false;
    int voice_id = 0;
    VoiceSettings settings;
    unsigned sample_rate = 22050;
    std::wstring text;
};
PreviewRequest g_request;

std::vector<char> build_wav(const std::vector<char>& pcm, unsigned rate,
                            unsigned bits, unsigned channels)
{
#pragma pack(push, 1)
    struct Header {
        char riff[4];
        unsigned riff_size;
        char wave[4];
        char fmt[4];
        unsigned fmt_size;
        unsigned short format;
        unsigned short channels;
        unsigned sample_rate;
        unsigned byte_rate;
        unsigned short block_align;
        unsigned short bits;
        char data[4];
        unsigned data_size;
    };
#pragma pack(pop)
    static_assert(sizeof(Header) == 44, "the WAV header must be 44 bytes");

    Header header = {{'R', 'I', 'F', 'F'},
                     static_cast<unsigned>(36 + pcm.size()),
                     {'W', 'A', 'V', 'E'},
                     {'f', 'm', 't', ' '},
                     16,
                     1,
                     static_cast<unsigned short>(channels),
                     rate,
                     rate * channels * bits / 8,
                     static_cast<unsigned short>(channels * bits / 8),
                     static_cast<unsigned short>(bits),
                     {'d', 'a', 't', 'a'},
                     static_cast<unsigned>(pcm.size())};

    std::vector<char> wav(sizeof(Header) + pcm.size());
    memcpy(wav.data(), &header, sizeof(Header));
    if (!pcm.empty()) {
        memcpy(wav.data() + sizeof(Header), pcm.data(), pcm.size());
    }
    return wav;
}

void set_status(const wchar_t* text)
{
    if (g_dialog) {
        SetDlgItemTextW(g_dialog, IDC_STATUS, text);
    }
}

DWORD WINAPI preview_loop(LPVOID)
{
    // The thread owns its own engine, so a preview never disturbs the host
    // an application is speaking through.
    Synth synth;
    std::vector<char> buffer;

    while (WaitForSingleObject(g_preview_event, INFINITE) == WAIT_OBJECT_0) {
        if (g_preview_quit) {
            break;
        }

        PreviewRequest request;
        EnterCriticalSection(&g_preview_lock);
        if (g_request.pending) {
            request = g_request;
            g_request.pending = false;
        }
        ResetEvent(g_preview_event);
        LeaveCriticalSection(&g_preview_lock);
        if (!request.pending) {
            continue;
        }

        // Stop whatever is playing before rendering the next one, so a burst
        // of changes does not queue up a backlog of stale samples.
        PlaySoundW(nullptr, nullptr, SND_PURGE);

        HostError error;
        if (!synth.ensure_host(request.sample_rate, &error)) {
            SV_LOG_ERROR("config: preview host unavailable - %S",
                         error.message.c_str());
            if (g_dialog) {
                SetDlgItemTextW(g_dialog, IDC_STATUS, error.message.c_str());
            }
            continue;
        }
        synth.apply(request.voice_id, request.settings, SpeechAdjust());

        buffer.clear();
        const bool ok =
            synth.speak(request.text, [&](const void* data, std::size_t n) {
                const auto* start = static_cast<const char*>(data);
                buffer.insert(buffer.end(), start, start + n);
                // Abandon this render the moment a newer one is asked for.
                return !g_request.pending && !g_preview_quit;
            });
        if (!ok || buffer.empty() || g_request.pending || g_preview_quit) {
            continue;
        }

        // Held for the lifetime of the playback: PlaySound with SND_ASYNC
        // reads from this buffer after it returns, so it cannot be a local.
        static std::vector<char> playing;
        playing = build_wav(buffer, synth.sample_rate(),
                            synth.bits_per_sample(), 1);
        PlaySoundW(reinterpret_cast<LPCWSTR>(playing.data()), nullptr,
                   SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    }
    PlaySoundW(nullptr, nullptr, SND_PURGE);
    return 0;
}

void request_preview(const std::wstring& text)
{
    EnterCriticalSection(&g_preview_lock);
    g_request.pending = true;
    g_request.voice_id = g_current;
    g_request.settings = g_settings.for_voice(g_voices[g_current].name());
    g_request.sample_rate = g_settings.sample_rate;
    g_request.text = text;
    SetEvent(g_preview_event);
    LeaveCriticalSection(&g_preview_lock);
}

// ------------------------------------------------------------- combo help --

void fill_choices(HWND dialog, int id, const params::Choice* choices,
                  std::size_t count, int selected)
{
    HWND combo = GetDlgItem(dialog, id);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (std::size_t i = 0; i < count; ++i) {
        const auto index = static_cast<int>(SendMessageW(
            combo, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(choices[i].label)));
        SendMessageW(combo, CB_SETITEMDATA, index, choices[i].value);
        if (choices[i].value == selected) {
            SendMessageW(combo, CB_SETCURSEL, index, 0);
        }
    }
    if (SendMessageW(combo, CB_GETCURSEL, 0, 0) == CB_ERR) {
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    }
}

int choice_value(HWND dialog, int id, int fallback)
{
    HWND combo = GetDlgItem(dialog, id);
    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index == CB_ERR) {
        return fallback;
    }
    return static_cast<int>(SendMessageW(combo, CB_GETITEMDATA, index, 0));
}

void set_number(HWND dialog, int id, int value)
{
    wchar_t text[16];
    swprintf_s(text, L"%d", value);
    SetDlgItemTextW(dialog, id, text);
}

int get_number(HWND dialog, int id, int fallback)
{
    wchar_t text[16] = {};
    if (GetDlgItemTextW(dialog, id, text, 16) == 0) {
        return fallback;
    }
    return _wtoi(text);
}

// --------------------------------------------------------------- the form --

void save()
{
    if (!save_settings(g_settings_path, g_settings)) {
        SV_LOG_ERROR("config: could not write %S", g_settings_path.c_str());
        set_status(L"The settings could not be saved. See the log.");
        return;
    }
    SV_LOG_DEBUG("config: saved %S", g_settings_path.c_str());
}

// The sentence the Speak test button uses, in the voice's own language.
std::wstring test_sentence(const Voice& voice)
{
    if (voice.language().bit == 0x2) {
        return std::wstring(L"Esta es la voz ") + voice.personality().name +
               L" de SoftVoice, con sus ajustes actuales.";
    }
    return std::wstring(L"This is the SoftVoice ") +
           voice.personality().name + L" voice, at your current settings.";
}

// Short, because it plays after every change when auto preview is on.
std::wstring change_sentence(const Voice& voice)
{
    return voice.language().bit == 0x2 ? L"Velocidad, tono y volumen."
                                       : L"Rate, pitch and volume.";
}

void load_voice_into_form(HWND dialog)
{
    g_loading = true;

    const Voice voice = g_voices[g_current];
    const VoiceSettings s = g_settings.for_voice(voice.name());

    set_number(dialog, IDC_RATE, s.rate);
    set_number(dialog, IDC_PITCH, s.pitch);
    set_number(dialog, IDC_VOLUME, s.volume);

    fill_choices(dialog, IDC_INFLECTION, kPercentChoices,
                 kPercentChoiceCount, s.inflection);
    fill_choices(dialog, IDC_BREATHINESS, kPercentChoices,
                 kPercentChoiceCount, s.breathiness);
    fill_choices(dialog, IDC_ROUGHNESS, kPercentChoices,
                 kPercentChoiceCount, s.roughness);
    fill_choices(dialog, IDC_VOWEL, kPercentChoices,
                 kPercentChoiceCount, s.vowel_length);

    fill_choices(dialog, IDC_GLOTTAL, params::kGlottalChoices,
                 params::kGlottalChoiceCount, s.glottal_source);
    fill_choices(dialog, IDC_INTONATION, params::kIntonationChoices,
                 params::kIntonationChoiceCount, s.intonation);
    fill_choices(dialog, IDC_VOICING, params::kVoicingChoices,
                 params::kVoicingChoiceCount, s.voicing);
    fill_choices(dialog, IDC_GENDER, params::kGenderChoices,
                 params::kGenderChoiceCount, s.gender);
    fill_choices(dialog, IDC_AVBIAS, kAvBiasChoices, kAvBiasChoiceCount,
                 s.av_bias);
    fill_choices(dialog, IDC_MAKEUP, kMakeupChoices, kMakeupChoiceCount,
                 s.volume_makeup);

    g_loading = false;
}

// Read every control back into the model, clamp it, and write the file.
// Called after each individual change: a partial edit is still a valid
// setting, and writing at once is what makes the change take effect on the
// next thing spoken.
void collect_and_save(HWND dialog, bool preview)
{
    if (g_loading) {
        return;
    }
    const Voice voice = g_voices[g_current];
    VoiceSettings s = g_settings.for_voice(voice.name());

    s.rate = get_number(dialog, IDC_RATE, s.rate);
    s.pitch = get_number(dialog, IDC_PITCH, s.pitch);
    s.volume = get_number(dialog, IDC_VOLUME, s.volume);

    s.inflection = choice_value(dialog, IDC_INFLECTION, s.inflection);
    s.breathiness = choice_value(dialog, IDC_BREATHINESS, s.breathiness);
    s.roughness = choice_value(dialog, IDC_ROUGHNESS, s.roughness);
    s.vowel_length = choice_value(dialog, IDC_VOWEL, s.vowel_length);

    s.glottal_source =
        choice_value(dialog, IDC_GLOTTAL, s.glottal_source);
    s.intonation = choice_value(dialog, IDC_INTONATION, s.intonation);
    s.voicing = choice_value(dialog, IDC_VOICING, s.voicing);
    s.gender = choice_value(dialog, IDC_GENDER, s.gender);
    s.av_bias = choice_value(dialog, IDC_AVBIAS, s.av_bias);
    s.volume_makeup = choice_value(dialog, IDC_MAKEUP, s.volume_makeup);

    s.clamp();
    g_settings.set_for_voice(voice.name(), s);
    g_settings.default_voice = voice.name();

    const int rate_index =
        static_cast<int>(SendDlgItemMessageW(dialog, IDC_SAMPLERATE,
                                             CB_GETCURSEL, 0, 0));
    if (rate_index >= 0 &&
        rate_index < static_cast<int>(kSampleRateCount)) {
        g_settings.sample_rate = kSampleRates[rate_index];
    }
    g_settings.log_level = static_cast<log::Level>(choice_value(
        dialog, IDC_LOGLEVEL, static_cast<int>(g_settings.log_level)));
    log::set_level(g_settings.log_level);

    save();

    // Show the clamped value back, so a typed 500 is seen and announced as
    // the 100 that was actually stored. Only the three edit fields need
    // this; a list cannot hold a value that was not offered.
    g_loading = true;
    set_number(dialog, IDC_RATE, s.rate);
    set_number(dialog, IDC_PITCH, s.pitch);
    set_number(dialog, IDC_VOLUME, s.volume);
    g_loading = false;

    set_status(L"Saved. The change applies to the next thing spoken.");

    if (preview &&
        IsDlgButtonChecked(dialog, IDC_AUTOPREVIEW) == BST_CHECKED) {
        request_preview(change_sentence(voice));
    }
}

void populate(HWND dialog)
{
    g_loading = true;

    HWND voices = GetDlgItem(dialog, IDC_VOICE);
    SendMessageW(voices, CB_RESETCONTENT, 0, 0);
    for (const Voice& voice : g_voices) {
        SendMessageW(voices, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(voice.name().c_str()));
    }
    SendMessageW(voices, CB_SETCURSEL, g_current, 0);

    HWND rates = GetDlgItem(dialog, IDC_SAMPLERATE);
    SendMessageW(rates, CB_RESETCONTENT, 0, 0);
    for (std::size_t i = 0; i < kSampleRateCount; ++i) {
        wchar_t text[32];
        swprintf_s(text, L"%u", kSampleRates[i]);
        SendMessageW(rates, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(text));
        if (kSampleRates[i] == g_settings.sample_rate) {
            SendMessageW(rates, CB_SETCURSEL, static_cast<WPARAM>(i), 0);
        }
    }
    if (SendMessageW(rates, CB_GETCURSEL, 0, 0) == CB_ERR) {
        SendMessageW(rates, CB_SETCURSEL, kSampleRateCount - 1, 0);
    }

    fill_choices(dialog, IDC_LOGLEVEL, kLogChoices, kLogChoiceCount,
                 static_cast<int>(g_settings.log_level));

    g_loading = false;
    load_voice_into_form(dialog);
}

INT_PTR CALLBACK dialog_proc(HWND dialog, UINT message, WPARAM wparam,
                             LPARAM lparam)
{
    switch (message) {
        case WM_INITDIALOG: {
            g_dialog = dialog;
            SendMessageW(dialog, WM_SETICON, ICON_BIG,
                         reinterpret_cast<LPARAM>(LoadIconW(
                             GetModuleHandleW(nullptr),
                             MAKEINTRESOURCEW(IDI_APPICON))));
            populate(dialog);
            set_status(L"Every change is saved and applied as you make it.");
            return TRUE;
        }

        case WM_COMMAND: {
            const int id = LOWORD(wparam);
            const int notification = HIWORD(wparam);

            if (id == IDC_VOICE && notification == CBN_SELCHANGE) {
                const LRESULT index =
                    SendDlgItemMessageW(dialog, IDC_VOICE, CB_GETCURSEL, 0, 0);
                if (index != CB_ERR &&
                    index < static_cast<LRESULT>(g_voices.size())) {
                    g_current = static_cast<int>(index);
                    g_settings.default_voice = g_voices[g_current].name();
                    load_voice_into_form(dialog);
                    save();
                    set_status(L"Now adjusting this voice.");
                    if (IsDlgButtonChecked(dialog, IDC_AUTOPREVIEW) ==
                        BST_CHECKED) {
                        request_preview(change_sentence(g_voices[g_current]));
                    }
                }
                return TRUE;
            }

            // A combo commits the moment the selection changes; an edit
            // commits when focus leaves it, which is what a person tabbing
            // through the dialog expects and avoids saving a half-typed
            // number on every keystroke.
            if (notification == CBN_SELCHANGE || notification == EN_KILLFOCUS) {
                switch (id) {
                    case IDC_RATE:
                    case IDC_PITCH:
                    case IDC_VOLUME:
                    case IDC_INFLECTION:
                    case IDC_BREATHINESS:
                    case IDC_ROUGHNESS:
                    case IDC_VOWEL:
                    case IDC_GLOTTAL:
                    case IDC_INTONATION:
                    case IDC_VOICING:
                    case IDC_GENDER:
                    case IDC_AVBIAS:
                    case IDC_MAKEUP:
                    case IDC_SAMPLERATE:
                    case IDC_LOGLEVEL:
                        collect_and_save(dialog, true);
                        return TRUE;
                    default:
                        break;
                }
            }

            switch (id) {
                case IDC_SPEAK:
                    collect_and_save(dialog, false);
                    request_preview(test_sentence(g_voices[g_current]));
                    set_status(L"Speaking a test sentence.");
                    return TRUE;

                case IDC_RESET: {
                    g_settings.set_for_voice(g_voices[g_current].name(),
                                             VoiceSettings());
                    load_voice_into_form(dialog);
                    save();
                    set_status(L"This voice is back to its default settings.");
                    return TRUE;
                }

                case IDC_OPENLOGS: {
                    const std::wstring dir = paths::log_dir();
                    ShellExecuteW(dialog, L"open", dir.c_str(), nullptr,
                                  nullptr, SW_SHOWNORMAL);
                    return TRUE;
                }

                case IDCANCEL:
                case IDOK:
                    // Collect once more on the way out, so a value typed and
                    // left focused when Done was pressed is not lost.
                    collect_and_save(dialog, false);
                    EndDialog(dialog, 0);
                    return TRUE;

                default:
                    break;
            }
            return FALSE;
        }

        case WM_CLOSE:
            collect_and_save(dialog, false);
            EndDialog(dialog, 0);
            return TRUE;

        default:
            return FALSE;
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    log::init(L"config");

    g_settings_path = paths::settings_file();
    if (!load_settings(g_settings_path, &g_settings)) {
        SV_LOG_INFO("config: no settings file yet; starting from defaults");
        g_settings = Settings();
    }
    log::set_level(g_settings.log_level);

    g_voices = available_voices();
    if (g_voices.empty()) {
        MessageBoxW(nullptr,
                    L"No SoftVoice voices are available. The SoftVoice engine "
                    L"files appear to be missing from the program folder.",
                    L"SoftVoice Speech Settings", MB_OK | MB_ICONERROR);
        return 1;
    }
    for (std::size_t i = 0; i < g_voices.size(); ++i) {
        if (_wcsicmp(g_voices[i].name().c_str(),
                     g_settings.default_voice.c_str()) == 0) {
            g_current = static_cast<int>(i);
            break;
        }
    }

    InitializeCriticalSection(&g_preview_lock);
    g_preview_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_preview_thread =
        CreateThread(nullptr, 0, preview_loop, nullptr, 0, nullptr);

    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_CONFIG), nullptr,
                    dialog_proc, 0);

    InterlockedExchange(&g_preview_quit, 1);
    if (g_preview_event) {
        SetEvent(g_preview_event);
    }
    if (g_preview_thread) {
        WaitForSingleObject(g_preview_thread, 5000);
        CloseHandle(g_preview_thread);
    }
    if (g_preview_event) {
        CloseHandle(g_preview_event);
    }
    DeleteCriticalSection(&g_preview_lock);

    log::shutdown();
    return 0;
}
