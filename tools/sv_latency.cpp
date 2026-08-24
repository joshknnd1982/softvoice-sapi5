// sv_latency - measure how long it takes to actually hear something.
//
// sv_selftest --time measures the engine in isolation. This measures the
// whole path an application uses: ISpVoice::Speak, SAPI's own dispatch and
// audio buffering, this engine, the host, and the audio device. Splitting
// the total between SAPI's share and the engine's share is what says whether
// making the engine faster is still worth doing.
//
// Timing is taken from SAPI's own events. SPEI_START_INPUT_STREAM fires when
// SAPI begins the utterance; word and sentence boundary events are delivered
// in step with playback, so the first of those is the moment sound reaches
// the speaker.
//
//   sv_latency [--voice NAME] [--repeat N] [--silent]

#include <windows.h>

#include <comdef.h>
#include <comip.h>
#include <cstdio>
#include <sapi.h>
#include <sperror.h>
#include <string>
#include <vector>

namespace {

_COM_SMARTPTR_TYPEDEF(ISpVoice, __uuidof(ISpVoice));
_COM_SMARTPTR_TYPEDEF(ISpObjectToken, __uuidof(ISpObjectToken));
_COM_SMARTPTR_TYPEDEF(ISpDataKey, __uuidof(ISpDataKey));
_COM_SMARTPTR_TYPEDEF(IEnumSpObjectTokens, __uuidof(IEnumSpObjectTokens));

double now_ms()
{
    static LARGE_INTEGER frequency = [] {
        LARGE_INTEGER f = {};
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER counter = {};
    QueryPerformanceCounter(&counter);
    return 1000.0 * static_cast<double>(counter.QuadPart) /
           static_cast<double>(frequency.QuadPart);
}

const wchar_t* const kPhrases[] = {
    L"Desktop",
    L"Recycle Bin",
    L"Documents folder",
    L"Start button, collapsed",
    L"Firefox, 3 of 12, list",
    L"Settings, dialog",
};
constexpr int kPhraseCount = 6;

HRESULT find_voice(const std::wstring& wanted, ISpObjectToken** out)
{
    ISpObjectTokenCategory* category = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr,
                                  CLSCTX_ALL,
                                  __uuidof(ISpObjectTokenCategory),
                                  reinterpret_cast<void**>(&category));
    if (FAILED(hr)) {
        return hr;
    }
    IEnumSpObjectTokensPtr tokens;
    hr = category->SetId(SPCAT_VOICES, FALSE);
    if (SUCCEEDED(hr)) {
        hr = category->EnumTokens(nullptr, nullptr, &tokens);
    }
    category->Release();
    if (FAILED(hr)) {
        return hr;
    }

    ULONG count = 0;
    tokens->GetCount(&count);
    for (ULONG i = 0; i < count; ++i) {
        ISpObjectTokenPtr token;
        if (FAILED(tokens->Item(i, &token))) {
            continue;
        }
        ISpDataKeyPtr attributes;
        WCHAR* name = nullptr;
        if (SUCCEEDED(token->OpenKey(L"Attributes", &attributes)) &&
            attributes) {
            attributes->GetStringValue(L"Name", &name);
        }
        if (!name) {
            continue;
        }
        const bool match = _wcsicmp(name, wanted.c_str()) == 0 ||
                           wcsstr(name, wanted.c_str()) != nullptr;
        CoTaskMemFree(name);
        if (match) {
            *out = token.Detach();
            return S_OK;
        }
    }
    return SPERR_NOT_FOUND;
}

}  // namespace

int wmain(int argc, wchar_t** argv)
{
    std::wstring wanted = L"SoftVoice Female";
    int repeat = 3;
    bool silent = false;

    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--voice" && i + 1 < argc) {
            wanted = argv[++i];
        } else if (arg == L"--repeat" && i + 1 < argc) {
            repeat = _wtoi(argv[++i]);
        } else if (arg == L"--silent") {
            silent = true;
        } else {
            wprintf(L"sv_latency [--voice NAME] [--repeat N] [--silent]\n");
            return 2;
        }
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        return 1;
    }

    int status = 0;
    do {
        ISpVoicePtr voice;
        if (FAILED(voice.CreateInstance(CLSID_SpVoice)) || !voice) {
            wprintf(L"could not create an ISpVoice\n");
            status = 1;
            break;
        }
        ISpObjectTokenPtr token;
        if (FAILED(find_voice(wanted, &token))) {
            wprintf(L"no SAPI voice matching \"%s\"\n", wanted.c_str());
            status = 1;
            break;
        }
        voice->SetVoice(token);

        // Word and sentence boundaries are delivered in step with playback,
        // so the first one is when sound actually reached the device.
        voice->SetInterest(SPFEI(SPEI_START_INPUT_STREAM) |
                               SPFEI(SPEI_WORD_BOUNDARY) |
                               SPFEI(SPEI_SENTENCE_BOUNDARY) |
                               SPFEI(SPEI_END_INPUT_STREAM),
                           SPFEI(SPEI_START_INPUT_STREAM) |
                               SPFEI(SPEI_WORD_BOUNDARY) |
                               SPFEI(SPEI_SENTENCE_BOUNDARY) |
                               SPFEI(SPEI_END_INPUT_STREAM));

        if (silent) {
            // Render to nowhere, so the numbers exclude the audio device.
            voice->SetOutput(nullptr, TRUE);
        }

        wprintf(L"Voice: %s%s\n\n", wanted.c_str(),
                silent ? L"  (no audio device)" : L"");
        wprintf(L"  %-26s %11s %11s %11s\n", L"phrase", L"Speak ret",
                L"engine start", L"first sound");

        double worst = 0.0;
        double total = 0.0;
        int measured = 0;

        for (int r = 0; r < repeat; ++r) {
            for (int p = 0; p < kPhraseCount; ++p) {
                const double begin = now_ms();
                hr = voice->Speak(kPhrases[p], SPF_ASYNC | SPF_PURGEBEFORESPEAK,
                                  nullptr);
                const double returned = now_ms() - begin;
                if (FAILED(hr)) {
                    wprintf(L"  Speak failed, hr 0x%08lX\n",
                            static_cast<unsigned long>(hr));
                    status = 1;
                    break;
                }

                double started = -1.0;
                double first_sound = -1.0;
                // Poll the event queue; SPEI_START_INPUT_STREAM tells us
                // SAPI has begun, the first boundary tells us it is audible.
                while (first_sound < 0.0 && now_ms() - begin < 5000.0) {
                    if (voice->WaitForNotifyEvent(20) != S_OK) {
                        continue;
                    }
                    SPEVENT event = {};
                    ULONG fetched = 0;
                    while (voice->GetEvents(1, &event, &fetched) == S_OK &&
                           fetched == 1) {
                        const double at = now_ms() - begin;
                        if (event.eEventId == SPEI_START_INPUT_STREAM &&
                            started < 0.0) {
                            started = at;
                        } else if ((event.eEventId == SPEI_WORD_BOUNDARY ||
                                    event.eEventId ==
                                        SPEI_SENTENCE_BOUNDARY) &&
                                   first_sound < 0.0) {
                            first_sound = at;
                        }
                        if (event.elParamType == SPET_LPARAM_IS_STRING &&
                            event.lParam) {
                            CoTaskMemFree(reinterpret_cast<void*>(event.lParam));
                        }
                        fetched = 0;
                    }
                }

                if (r == repeat - 1) {
                    wprintf(L"  %-26s %8.1f ms %8.1f ms %8.1f ms\n",
                            kPhrases[p], returned, started, first_sound);
                }
                if (first_sound > 0.0) {
                    worst = first_sound > worst ? first_sound : worst;
                    total += first_sound;
                    ++measured;
                }
                voice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
                Sleep(120);
            }
        }

        if (measured) {
            wprintf(L"\n  average time to first sound: %6.1f ms over %d "
                    L"phrases\n",
                    total / measured, measured);
            wprintf(L"  worst:                       %6.1f ms\n", worst);
        }

        // The arrow-key case, and the only one that is really felt: speech
        // is already running when the next item is announced. Everything
        // above starts from silence, which is the easy case.
        wprintf(L"\n  Interrupting speech that is already playing:\n");
        const wchar_t* kLong =
            L"This is a long sentence that is still being spoken when the "
            L"next item is selected, so that interrupting it is measured "
            L"rather than merely starting from silence.";
        double interrupt_total = 0.0;
        double interrupt_worst = 0.0;
        int interrupts = 0;

        for (int r = 0; r < repeat * 2; ++r) {
            voice->Speak(kLong, SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
            Sleep(250);   // let it get properly under way
            while (voice->WaitForNotifyEvent(0) == S_OK) {
                SPEVENT drain = {};
                ULONG got = 0;
                while (voice->GetEvents(1, &drain, &got) == S_OK && got == 1) {
                    if (drain.elParamType == SPET_LPARAM_IS_STRING &&
                        drain.lParam) {
                        CoTaskMemFree(reinterpret_cast<void*>(drain.lParam));
                    }
                    got = 0;
                }
            }

            const double begin = now_ms();
            voice->Speak(kPhrases[r % kPhraseCount],
                         SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
            double sound = -1.0;
            while (sound < 0.0 && now_ms() - begin < 5000.0) {
                if (voice->WaitForNotifyEvent(20) != S_OK) {
                    continue;
                }
                SPEVENT event = {};
                ULONG fetched = 0;
                while (voice->GetEvents(1, &event, &fetched) == S_OK &&
                       fetched == 1) {
                    if ((event.eEventId == SPEI_WORD_BOUNDARY ||
                         event.eEventId == SPEI_SENTENCE_BOUNDARY) &&
                        sound < 0.0) {
                        sound = now_ms() - begin;
                    }
                    if (event.elParamType == SPET_LPARAM_IS_STRING &&
                        event.lParam) {
                        CoTaskMemFree(reinterpret_cast<void*>(event.lParam));
                    }
                    fetched = 0;
                }
            }
            if (sound > 0.0) {
                wprintf(L"    interrupt %d -> heard %.1f ms later\n", r + 1,
                        sound);
                interrupt_total += sound;
                interrupt_worst =
                    sound > interrupt_worst ? sound : interrupt_worst;
                ++interrupts;
            }
            voice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
            Sleep(120);
        }
        if (interrupts) {
            wprintf(L"\n  average interrupt to sound:  %6.1f ms over %d\n",
                    interrupt_total / interrupts, interrupts);
            wprintf(L"  worst:                       %6.1f ms\n",
                    interrupt_worst);
        }
    } while (false);

    CoUninitialize();
    return status;
}
