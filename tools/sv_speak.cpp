// sv_speak - speak, or render, through SAPI 5.
//
// Where sv_render proves the engine works, this proves the SAPI 5 interface
// works: it goes through the same COM registration, the same voice
// enumerator and the same ISpTTSEngine an application would use. Built for
// both architectures, so the 32-bit and 64-bit DLLs can each be exercised on
// their own.
//
//   sv_speak --list
//   sv_speak --voice "SoftVoice Female" --text "Hello."
//   sv_speak --voice "SoftVoice Male (Spanish)" --text "Hola." --out hola.wav
//
// Deliberately built on plain COM rather than the ATL helpers in sphelper.h:
// ATL is not part of the Build Tools workload, and requiring it would make
// this package harder to build than it needs to be.

#include <comdef.h>
#include <comip.h>
#include <cstdio>
#include <sapi.h>
#include <sperror.h>
#include <shlwapi.h>
#include <string>
#include <windows.h>

#pragma comment(lib, "shlwapi.lib")

namespace {

_COM_SMARTPTR_TYPEDEF(ISpVoice, __uuidof(ISpVoice));
_COM_SMARTPTR_TYPEDEF(ISpStream, __uuidof(ISpStream));
_COM_SMARTPTR_TYPEDEF(ISpObjectToken, __uuidof(ISpObjectToken));
_COM_SMARTPTR_TYPEDEF(ISpDataKey, __uuidof(ISpDataKey));
_COM_SMARTPTR_TYPEDEF(IEnumSpObjectTokens, __uuidof(IEnumSpObjectTokens));

void usage()
{
    wprintf(
        L"sv_speak - speak or render through SAPI 5\n"
        L"\n"
        L"  sv_speak --list\n"
        L"  sv_speak [--voice NAME] [--text TEXT] [--out FILE.wav]\n"
        L"           [--rate N] [--volume N]\n"
        L"\n"
        L"  --list        every SAPI 5 voice Windows can see, SoftVoice or not\n"
        L"  --voice NAME  a full or partial voice name\n"
        L"  --text TEXT   what to say\n"
        L"  --out FILE    render to a WAV instead of playing it\n"
        L"  --rate N      SAPI rate, -10 to 10\n"
        L"  --volume N    SAPI volume, 0 to 100\n");
}

// A CoTaskMemFree'd string an interface handed back.
class heap_string {
public:
    ~heap_string()
    {
        if (value_) {
            CoTaskMemFree(value_);
        }
    }
    WCHAR** address() { return &value_; }
    [[nodiscard]] const WCHAR* get() const { return value_; }
    [[nodiscard]] const WCHAR* or_else(const WCHAR* fallback) const
    {
        return value_ ? value_ : fallback;
    }

private:
    WCHAR* value_ = nullptr;
};

void read_attributes(ISpObjectToken* token, heap_string* name,
                     heap_string* language, heap_string* gender,
                     heap_string* vendor)
{
    ISpDataKeyPtr attributes;
    if (FAILED(token->OpenKey(L"Attributes", &attributes)) || !attributes) {
        return;
    }
    attributes->GetStringValue(L"Name", name->address());
    attributes->GetStringValue(L"Language", language->address());
    attributes->GetStringValue(L"Gender", gender->address());
    attributes->GetStringValue(L"Vendor", vendor->address());
}

HRESULT enumerate(IEnumSpObjectTokens** out)
{
    ISpObjectTokenPtr category;
    // SpEnumTokens lives in sphelper.h, so the category is opened directly.
    ISpObjectTokenCategory* raw = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr,
                                  CLSCTX_ALL, __uuidof(ISpObjectTokenCategory),
                                  reinterpret_cast<void**>(&raw));
    if (FAILED(hr)) {
        return hr;
    }
    hr = raw->SetId(SPCAT_VOICES, FALSE);
    if (SUCCEEDED(hr)) {
        hr = raw->EnumTokens(nullptr, nullptr, out);
    }
    raw->Release();
    return hr;
}

HRESULT list_voices()
{
    IEnumSpObjectTokensPtr tokens;
    HRESULT hr = enumerate(&tokens);
    if (FAILED(hr)) {
        wprintf(L"error: could not enumerate voices, hr 0x%08lX\n",
                static_cast<unsigned long>(hr));
        return hr;
    }

    ULONG count = 0;
    tokens->GetCount(&count);
    wprintf(L"%lu SAPI 5 voices:\n\n", count);

    ULONG softvoice = 0;
    for (ULONG i = 0; i < count; ++i) {
        ISpObjectTokenPtr token;
        if (FAILED(tokens->Item(i, &token))) {
            continue;
        }
        heap_string name, language, gender, vendor;
        read_attributes(token, &name, &language, &gender, &vendor);
        wprintf(L"  %-36s lang %-6s %-8s %s\n", name.or_else(L"(no name)"),
                language.or_else(L"?"), gender.or_else(L"?"),
                vendor.or_else(L""));
        if (vendor.get() && _wcsicmp(vendor.get(), L"SoftVoice") == 0) {
            ++softvoice;
        }
    }
    wprintf(L"\n%lu of them are SoftVoice voices.\n", softvoice);
    return softvoice ? S_OK : E_FAIL;
}

HRESULT find_voice(const std::wstring& wanted, ISpObjectToken** out)
{
    IEnumSpObjectTokensPtr tokens;
    HRESULT hr = enumerate(&tokens);
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
        heap_string name, language, gender, vendor;
        read_attributes(token, &name, &language, &gender, &vendor);
        if (!name.get()) {
            continue;
        }
        // A partial match, so "Female" or "Spanish" is enough to pick a
        // voice from the command line.
        if (_wcsicmp(name.get(), wanted.c_str()) == 0 ||
            StrStrIW(name.get(), wanted.c_str()) != nullptr) {
            *out = token.Detach();
            return S_OK;
        }
    }
    return SPERR_NOT_FOUND;
}

}  // namespace

int wmain(int argc, wchar_t** argv)
{
    std::wstring voice_name;
    std::wstring text = L"SoftVoice, speaking through SAPI 5.";
    std::wstring out;
    long rate = 0;
    USHORT volume = 100;
    bool list = false;

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
            list = true;
        } else if (arg == L"--voice") {
            voice_name = next(i);
        } else if (arg == L"--text") {
            text = next(i);
        } else if (arg == L"--out") {
            out = next(i);
        } else if (arg == L"--rate") {
            rate = _wtol(next(i).c_str());
        } else if (arg == L"--volume") {
            volume = static_cast<USHORT>(_wtoi(next(i).c_str()));
        } else {
            wprintf(L"error: unknown option %s\n", arg.c_str());
            return 2;
        }
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        wprintf(L"error: CoInitializeEx failed, hr 0x%08lX\n",
                static_cast<unsigned long>(hr));
        return 1;
    }

    int status = 0;
    if (list) {
        status = SUCCEEDED(list_voices()) ? 0 : 1;
        CoUninitialize();
        return status;
    }

    do {
        ISpVoicePtr speech;
        hr = speech.CreateInstance(CLSID_SpVoice);
        if (FAILED(hr) || !speech) {
            wprintf(L"error: could not create an ISpVoice, hr 0x%08lX\n",
                    static_cast<unsigned long>(hr));
            status = 1;
            break;
        }

        if (!voice_name.empty()) {
            ISpObjectTokenPtr token;
            hr = find_voice(voice_name, &token);
            if (FAILED(hr)) {
                wprintf(L"error: no SAPI voice matching \"%s\"; try --list\n",
                        voice_name.c_str());
                status = 1;
                break;
            }
            hr = speech->SetVoice(token);
            if (FAILED(hr)) {
                wprintf(L"error: SetVoice failed, hr 0x%08lX\n",
                        static_cast<unsigned long>(hr));
                status = 1;
                break;
            }
        }

        speech->SetRate(rate);
        speech->SetVolume(volume);

        ISpStreamPtr stream;
        if (!out.empty()) {
            WAVEFORMATEX format = {};
            format.wFormatTag = WAVE_FORMAT_PCM;
            format.nChannels = 1;
            format.nSamplesPerSec = 22050;
            format.wBitsPerSample = 16;
            format.nBlockAlign = 2;
            format.nAvgBytesPerSec = 44100;
            format.cbSize = 0;

            hr = stream.CreateInstance(CLSID_SpStream);
            if (SUCCEEDED(hr)) {
                hr = stream->BindToFile(out.c_str(), SPFM_CREATE_ALWAYS,
                                        &SPDFID_WaveFormatEx, &format,
                                        SPFEI_ALL_EVENTS);
            }
            if (FAILED(hr)) {
                wprintf(L"error: could not open %s, hr 0x%08lX\n",
                        out.c_str(), static_cast<unsigned long>(hr));
                status = 1;
                break;
            }
            speech->SetOutput(stream, TRUE);
        }

        hr = speech->Speak(text.c_str(), SPF_DEFAULT, nullptr);
        if (FAILED(hr)) {
            wprintf(L"error: Speak failed, hr 0x%08lX\n",
                    static_cast<unsigned long>(hr));
            status = 1;
            break;
        }
        speech->WaitUntilDone(INFINITE);

        if (stream) {
            stream->Close();
            wprintf(L"wrote %s\n", out.c_str());
        } else {
            wprintf(L"spoke %zu characters\n", text.size());
        }
    } while (false);

    CoUninitialize();
    return status;
}
