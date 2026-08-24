// sv_selftest - exercise the SAPI 5 objects without registering anything.
//
// The DLL is loaded by path and its class objects are asked for directly, so
// the whole SAPI implementation - the voice enumerator, the tokens it builds,
// ISpObjectWithToken, GetOutputFormat and Speak - is tested exactly as SAPI
// would drive it, but with no HKLM registration and therefore no elevation.
//
// That matters twice over: it is how the interface can be checked during
// development, and it is how a failure can later be told apart from a
// registration problem, since this path shares everything with the real one
// except the registry.
//
//   sv_selftest [--dll PATH] [--out FILE.wav] [--voice NAME] [--all]

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <sapi.h>
#include <sapiddk.h>
#include <sperror.h>
#include <string>
#include <vector>

namespace {

// Must match the declspec(uuid) values in sv_enum_tokens.hpp and
// sv_engine.hpp. Repeated here rather than included, so the test links
// against nothing from the DLL and can be pointed at any build of it.
// clang-format off
const CLSID kEnumeratorClsid =
    {0x7C4A9E51, 0x2D68, 0x4B3F, {0x9E, 0x07, 0xC1, 0xA5, 0xB8, 0xD2, 0xF6, 0x40}};
const CLSID kEngineClsid =
    {0xB6E31F84, 0x5A0D, 0x4C72, {0x9F, 0x13, 0x8D, 0x4A, 0x7C, 0x25, 0xE0, 0xB9}};
// clang-format on

int g_failures = 0;

void check(bool condition, const wchar_t* what)
{
    wprintf(L"  %-58s %s\n", what, condition ? L"ok" : L"FAILED");
    if (!condition) {
        ++g_failures;
    }
}

// A minimal ISpTTSEngineSite: collects the audio and the events the engine
// produces, and answers the questions it asks.
class TestSite : public ISpTTSEngineSite {
public:
    TestSite(long rate, USHORT volume, ULONGLONG interest)
        : rate_(rate), volume_(volume), interest_(interest)
    {
    }

    // IUnknown. The site is a stack object owned by the caller, so the
    // counts exist only to satisfy anything that addrefs it.
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override
    {
        if (!ppv) {
            return E_POINTER;
        }
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, __uuidof(ISpEventSink)) ||
            IsEqualIID(riid, __uuidof(ISpTTSEngineSite))) {
            *ppv = static_cast<ISpTTSEngineSite*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() override { return ++refs_; }
    STDMETHOD_(ULONG, Release)() override { return --refs_; }

    // ISpEventSink
    STDMETHOD(AddEvents)(const SPEVENT* events, ULONG count) override
    {
        for (ULONG i = 0; i < count; ++i) {
            switch (events[i].eEventId) {
                case SPEI_WORD_BOUNDARY: ++words_; break;
                case SPEI_SENTENCE_BOUNDARY: ++sentences_; break;
                case SPEI_TTS_BOOKMARK: ++bookmarks_; break;
                default: break;
            }
            offsets_.push_back(events[i].ullAudioStreamOffset);
        }
        return S_OK;
    }
    STDMETHOD(GetEventInterest)(ULONGLONG* interest) override
    {
        *interest = interest_;
        return S_OK;
    }

    // ISpTTSEngineSite
    STDMETHOD_(DWORD, GetActions)() override
    {
        if (stop_after_ && audio_.size() >= stop_after_) {
            return SPVES_ABORT;
        }
        if (abort_ms_ && GetTickCount() - clock_started_ >= abort_ms_) {
            return SPVES_ABORT;
        }
        return SPVES_CONTINUE;
    }
    STDMETHOD(Write)(const void* data, ULONG count, ULONG* written) override
    {
        if (first_write_ms_ == 0xFFFFFFFF) {
            first_write_ms_ = GetTickCount() - clock_started_;
        }
        const auto* start = static_cast<const char*>(data);
        audio_.insert(audio_.end(), start, start + count);
        if (written) {
            *written = count;
        }
        return S_OK;
    }
    STDMETHOD(GetRate)(long* rate) override
    {
        *rate = rate_;
        return S_OK;
    }
    STDMETHOD(GetVolume)(USHORT* volume) override
    {
        *volume = volume_;
        return S_OK;
    }
    STDMETHOD(GetSkipInfo)(SPVSKIPTYPE* type, long* items) override
    {
        *type = SPVST_SENTENCE;
        *items = 0;
        return S_OK;
    }
    STDMETHOD(CompleteSkip)(long) override { return S_OK; }

    void abort_after(std::size_t bytes) { stop_after_ = bytes; }

    // Abort once this many milliseconds have passed since the clock was
    // started, which is how an arrow key arriving mid-sentence behaves.
    void abort_after_ms(DWORD ms)
    {
        abort_ms_ = ms;
        clock_started_ = GetTickCount();
    }

    [[nodiscard]] DWORD first_write_ms() const { return first_write_ms_; }

    [[nodiscard]] const std::vector<char>& audio() const { return audio_; }
    [[nodiscard]] ULONG words() const { return words_; }
    [[nodiscard]] ULONG sentences() const { return sentences_; }
    [[nodiscard]] ULONG bookmarks() const { return bookmarks_; }
    [[nodiscard]] bool offsets_ascend() const
    {
        for (std::size_t i = 1; i < offsets_.size(); ++i) {
            if (offsets_[i] < offsets_[i - 1]) {
                return false;
            }
        }
        return true;
    }

    void start_clock() { clock_started_ = GetTickCount(); }

private:
    ULONG refs_ = 1;
    long rate_ = 0;
    USHORT volume_ = 100;
    ULONGLONG interest_ = 0;
    std::size_t stop_after_ = 0;
    DWORD abort_ms_ = 0;
    DWORD clock_started_ = 0;
    DWORD first_write_ms_ = 0xFFFFFFFF;
    std::vector<char> audio_;
    std::vector<ULONGLONG> offsets_;
    ULONG words_ = 0;
    ULONG sentences_ = 0;
    ULONG bookmarks_ = 0;
};

using DllGetClassObjectFn = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);

DllGetClassObjectFn g_get_class_object = nullptr;

template <class I>
HRESULT create(const CLSID& clsid, I** out)
{
    IClassFactory* factory = nullptr;
    HRESULT hr = g_get_class_object(clsid, IID_IClassFactory,
                                    reinterpret_cast<void**>(&factory));
    if (FAILED(hr)) {
        return hr;
    }
    hr = factory->CreateInstance(nullptr, __uuidof(I),
                                 reinterpret_cast<void**>(out));
    factory->Release();
    return hr;
}

bool write_wav(const std::wstring& path, const std::vector<char>& pcm,
               const WAVEFORMATEX& format)
{
#pragma pack(push, 1)
    struct Header {
        char riff[4];
        unsigned riff_size;
        char wave[4];
        char fmt[4];
        unsigned fmt_size;
        unsigned short tag;
        unsigned short channels;
        unsigned rate;
        unsigned byte_rate;
        unsigned short align;
        unsigned short bits;
        char data[4];
        unsigned data_size;
    };
#pragma pack(pop)
    Header header = {{'R', 'I', 'F', 'F'},
                     static_cast<unsigned>(36 + pcm.size()),
                     {'W', 'A', 'V', 'E'},
                     {'f', 'm', 't', ' '},
                     16,
                     1,
                     format.nChannels,
                     format.nSamplesPerSec,
                     format.nAvgBytesPerSec,
                     format.nBlockAlign,
                     format.wBitsPerSample,
                     {'d', 'a', 't', 'a'},
                     static_cast<unsigned>(pcm.size())};

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    WriteFile(file, &header, sizeof(header), &written, nullptr);
    if (!pcm.empty()) {
        WriteFile(file, pcm.data(), static_cast<DWORD>(pcm.size()), &written,
                  nullptr);
    }
    CloseHandle(file);
    return true;
}

// What a screen reader says while someone arrows around, and one long line
// to interrupt.
const wchar_t* const kShortPhrases[] = {
    L"Desktop",
    L"Recycle Bin",
    L"Documents folder",
    L"Start button, collapsed",
    L"Firefox, 3 of 12, list",
    L"Settings, dialog",
};
constexpr int kShortPhraseCount = 6;

const wchar_t* const kLongPhrase =
    L"This is a long sentence, the sort a document reader would send, and it "
    L"is here so that interrupting it partway through is clearly different "
    L"from letting it run all the way to the end.";

// Build the fragment chain SAPI would hand the engine for one sentence,
// optionally preceded by a bookmark.
SPVTEXTFRAG make_fragment(const wchar_t* text, ULONG offset)
{
    SPVTEXTFRAG fragment = {};
    fragment.pNext = nullptr;
    fragment.State.eAction = SPVA_Speak;
    fragment.State.LangID = 0x409;
    fragment.State.EmphAdj = 0;
    fragment.State.RateAdj = 0;
    fragment.State.Volume = 100;
    fragment.State.PitchAdj.MiddleAdj = 0;
    fragment.State.PitchAdj.RangeAdj = 0;
    fragment.State.SilenceMSecs = 0;
    fragment.pTextStart = text;
    fragment.ulTextLen = static_cast<ULONG>(wcslen(text));
    fragment.ulTextSrcOffset = offset;
    return fragment;
}

// How responsive the voice is, measured the way it is felt: how long until
// the first audio reaches SAPI, how long the engine is occupied per phrase,
// and - the one that decides whether arrowing around feels immediate - how
// quickly an interruption is acted on.
int run_timing(ISpObjectToken* token, bool want_words)
{
    ISpTTSEngine* engine = nullptr;
    if (FAILED(create(kEngineClsid, &engine)) || !engine) {
        wprintf(L"could not create the engine\n");
        return 1;
    }
    ISpObjectWithToken* with_token = nullptr;
    engine->QueryInterface(__uuidof(ISpObjectWithToken),
                           reinterpret_cast<void**>(&with_token));
    if (with_token) {
        with_token->SetObjectToken(token);
        with_token->Release();
    }
    GUID format_id = {};
    WAVEFORMATEX* format = nullptr;
    engine->GetOutputFormat(nullptr, nullptr, &format_id, &format);

    const ULONGLONG interest =
        want_words ? (SPFEI(SPEI_WORD_BOUNDARY) | SPFEI(SPEI_SENTENCE_BOUNDARY))
                   : 0;
    wprintf(L"\n%s word boundary events:\n",
            want_words ? L"Asking for" : L"Not asking for");
    wprintf(L"  %-30s %12s %12s %10s\n", L"phrase", L"first audio",
            L"Speak total", L"audio");

    DWORD worst_first = 0;
    DWORD total_all = 0;
    for (int i = 0; i < kShortPhraseCount; ++i) {
        TestSite site(0, 100, interest);
        site.start_clock();
        SPVTEXTFRAG fragment = make_fragment(kShortPhrases[i], 0);
        const DWORD begin = GetTickCount();
        engine->Speak(SPF_DEFAULT, GUID_NULL, format, &fragment, &site);
        const DWORD elapsed = GetTickCount() - begin;
        const double seconds =
            format ? site.audio().size() / 2.0 / format->nSamplesPerSec : 0.0;
        wprintf(L"  %-30s %9lu ms %9lu ms %7.2f s\n", kShortPhrases[i],
                site.first_write_ms(), elapsed, seconds);
        worst_first = (std::max)(worst_first, site.first_write_ms());
        total_all += elapsed;
    }
    wprintf(L"  %-30s %9lu ms %9lu ms\n", L"worst / total", worst_first,
            total_all);
    wprintf(L"  %-30s %9.1f phrases per second\n", L"throughput",
            total_all ? kShortPhraseCount * 1000.0 / total_all : 0.0);

    // The arrow-key case: speech is already running when the next key
    // arrives, and nothing can be said until Speak returns.
    wprintf(L"\n  Interrupting a long sentence:\n");
    for (const DWORD at : {0u, 50u, 150u}) {
        TestSite site(0, 100, interest);
        site.start_clock();
        site.abort_after_ms(at);
        SPVTEXTFRAG fragment = make_fragment(kLongPhrase, 0);
        const DWORD begin = GetTickCount();
        engine->Speak(SPF_DEFAULT, GUID_NULL, format, &fragment, &site);
        wprintf(L"    abort at %4lu ms -> Speak returned %lu ms later\n", at,
                GetTickCount() - begin);
    }

    if (format) {
        CoTaskMemFree(format);
    }
    engine->Release();
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv)
{
    std::wstring dll = L"SoftVoiceSAPI.dll";
    std::wstring out;
    std::wstring wanted;
    bool all = false;
    bool timing = false;

    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--dll" && i + 1 < argc) {
            dll = argv[++i];
        } else if (arg == L"--out" && i + 1 < argc) {
            out = argv[++i];
        } else if (arg == L"--voice" && i + 1 < argc) {
            wanted = argv[++i];
        } else if (arg == L"--all") {
            all = true;
        } else if (arg == L"--time") {
            timing = true;
        } else {
            wprintf(L"sv_selftest [--dll PATH] [--out FILE.wav] "
                    L"[--voice NAME] [--all] [--time]\n");
            return 2;
        }
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        wprintf(L"CoInitializeEx failed, hr 0x%08lX\n",
                static_cast<unsigned long>(hr));
        return 1;
    }

    wprintf(L"Loading %s\n", dll.c_str());
    HMODULE module = LoadLibraryW(dll.c_str());
    if (!module) {
        wprintf(L"could not load the DLL, error %lu\n", GetLastError());
        return 1;
    }
    g_get_class_object = reinterpret_cast<DllGetClassObjectFn>(
        GetProcAddress(module, "DllGetClassObject"));
    if (!g_get_class_object) {
        wprintf(L"the DLL exports no DllGetClassObject\n");
        return 1;
    }

    wprintf(L"\nEnumerator\n");
    IEnumSpObjectTokens* tokens = nullptr;
    hr = create(kEnumeratorClsid, &tokens);
    check(SUCCEEDED(hr) && tokens, L"the voice enumerator can be created");
    if (!tokens) {
        return 1;
    }

    ULONG count = 0;
    tokens->GetCount(&count);
    wprintf(L"  %lu voices published\n", count);
    check(count == 40 || count == 20,
          L"publishes 40 voices, or 20 without Svspan32.dll");

    // Every token must carry the attributes an application filters on, and
    // every name must be distinct or SAPI cannot tell them apart.
    std::vector<std::wstring> names;
    bool attributes_ok = true;
    for (ULONG i = 0; i < count; ++i) {
        ISpObjectToken* token = nullptr;
        if (FAILED(tokens->Item(i, &token)) || !token) {
            attributes_ok = false;
            continue;
        }
        ISpDataKey* attributes = nullptr;
        if (SUCCEEDED(token->OpenKey(L"Attributes", &attributes)) &&
            attributes) {
            WCHAR* name = nullptr;
            WCHAR* language = nullptr;
            WCHAR* gender = nullptr;
            attributes->GetStringValue(L"Name", &name);
            attributes->GetStringValue(L"Language", &language);
            attributes->GetStringValue(L"Gender", &gender);
            if (!name || !language || !gender) {
                attributes_ok = false;
            }
            if (name) {
                names.emplace_back(name);
                CoTaskMemFree(name);
            }
            if (language) CoTaskMemFree(language);
            if (gender) CoTaskMemFree(gender);
            attributes->Release();
        } else {
            attributes_ok = false;
        }
        token->Release();
    }
    check(attributes_ok, L"every token has Name, Language and Gender");

    bool unique = true;
    for (std::size_t i = 0; i < names.size() && unique; ++i) {
        for (std::size_t j = i + 1; j < names.size(); ++j) {
            if (_wcsicmp(names[i].c_str(), names[j].c_str()) == 0) {
                unique = false;
                break;
            }
        }
    }
    check(unique, L"every voice name is unique");

    // Clone and Reset have to behave, because SAPI uses them to walk the
    // list more than once.
    IEnumSpObjectTokens* clone = nullptr;
    check(SUCCEEDED(tokens->Clone(&clone)) && clone,
          L"the enumerator can be cloned");
    if (clone) {
        ULONG clone_count = 0;
        clone->GetCount(&clone_count);
        check(clone_count == count, L"the clone publishes the same count");
        clone->Release();
    }
    check(SUCCEEDED(tokens->Reset()), L"the enumerator resets");

    if (timing) {
        ISpObjectToken* token = nullptr;
        ULONG index = 1;  // Female, so a failure is obvious by ear
        for (ULONG i = 0; i < names.size(); ++i) {
            if (!wanted.empty() &&
                _wcsicmp(names[i].c_str(), wanted.c_str()) == 0) {
                index = i;
            }
        }
        if (FAILED(tokens->Item(index, &token)) || !token) {
            wprintf(L"could not fetch a token to time\n");
            return 1;
        }
        wprintf(L"\nTiming %s\n", names[index].c_str());
        // Both paths, because they differ: the word-boundary one has to hold
        // the audio back until it knows where the words fall.
        run_timing(token, false);
        run_timing(token, true);
        token->Release();
        tokens->Release();
        CoUninitialize();
        return 0;
    }

    // ------------------------------------------------------------- speech
    std::vector<ULONG> to_speak;
    if (all) {
        for (ULONG i = 0; i < count; ++i) {
            to_speak.push_back(i);
        }
    } else if (!wanted.empty()) {
        for (ULONG i = 0; i < names.size(); ++i) {
            if (_wcsicmp(names[i].c_str(), wanted.c_str()) == 0) {
                to_speak.push_back(i);
            }
        }
        if (to_speak.empty()) {
            wprintf(L"\nno voice named \"%s\"\n", wanted.c_str());
            return 1;
        }
    } else {
        to_speak.push_back(1);  // Female, so a failure is obvious by ear
    }

    wprintf(L"\nSpeech\n");
    for (const ULONG index : to_speak) {
        ISpObjectToken* token = nullptr;
        if (FAILED(tokens->Item(index, &token)) || !token) {
            check(false, L"the token could be fetched");
            continue;
        }

        ISpTTSEngine* engine = nullptr;
        hr = create(kEngineClsid, &engine);
        if (FAILED(hr) || !engine) {
            check(false, L"the TTS engine can be created");
            token->Release();
            continue;
        }

        ISpObjectWithToken* with_token = nullptr;
        engine->QueryInterface(__uuidof(ISpObjectWithToken),
                               reinterpret_cast<void**>(&with_token));
        if (with_token) {
            hr = with_token->SetObjectToken(token);
            ISpObjectToken* readback = nullptr;
            const bool round_trips =
                SUCCEEDED(with_token->GetObjectToken(&readback)) && readback;
            if (readback) {
                readback->Release();
            }
            if (to_speak.size() == 1) {
                check(SUCCEEDED(hr), L"SetObjectToken accepts the token");
                check(round_trips, L"GetObjectToken returns it again");
            }
            with_token->Release();
        }

        GUID format_id = {};
        WAVEFORMATEX* format = nullptr;
        hr = engine->GetOutputFormat(nullptr, nullptr, &format_id, &format);
        const bool format_ok = SUCCEEDED(hr) && format &&
                               format->nChannels == 1 &&
                               format->wBitsPerSample == 16;
        if (to_speak.size() == 1) {
            check(format_ok, L"GetOutputFormat reports 16-bit mono PCM");
        }

        // Word boundaries are asked for here on purpose: it is the path an
        // application that highlights text takes, and the one that has to
        // hold the fragment back to place the events.
        const ULONGLONG interest =
            SPFEI(SPEI_WORD_BOUNDARY) | SPFEI(SPEI_SENTENCE_BOUNDARY) |
            SPFEI(SPEI_TTS_BOOKMARK);
        TestSite site(0, 100, interest);

        SPVTEXTFRAG speech = make_fragment(
            L"SoftVoice is speaking through the SAPI five interface.", 0);
        SPVTEXTFRAG bookmark = {};
        bookmark.State.eAction = SPVA_Bookmark;
        bookmark.pTextStart = L"42";
        bookmark.ulTextLen = 2;
        bookmark.pNext = &speech;

        hr = engine->Speak(SPF_DEFAULT, GUID_NULL, format, &bookmark, &site);
        const double seconds =
            format ? site.audio().size() / 2.0 / format->nSamplesPerSec : 0.0;

        wprintf(L"  %-34s %6.2f s  %5zu bytes  %2lu words, %lu sentences, "
                L"%lu bookmarks\n",
                index < names.size() ? names[index].c_str() : L"?", seconds,
                site.audio().size(), site.words(), site.sentences(),
                site.bookmarks());

        if (to_speak.size() == 1) {
            check(SUCCEEDED(hr), L"Speak returns success");
            check(!site.audio().empty(), L"Speak produced audio");
            check(seconds > 1.0,
                  L"the audio is a whole sentence, not a clipped 0.4 s");
            check(site.words() >= 8, L"a word boundary per word was reported");
            check(site.sentences() == 1, L"one sentence boundary was reported");
            check(site.bookmarks() == 1, L"the bookmark was reported");
            check(site.offsets_ascend(),
                  L"event offsets never go backwards");

            // Aborting mid-utterance must stop promptly rather than render
            // the rest, which is what a screen reader depends on.
            TestSite aborting(0, 100, 0);
            aborting.abort_after(8000);
            SPVTEXTFRAG lots = make_fragment(
                L"This is a much longer sentence, long enough that stopping "
                L"part of the way through it is clearly different from "
                L"letting it run all the way to the end of the text.",
                0);
            engine->Speak(SPF_DEFAULT, GUID_NULL, format, &lots, &aborting);
            check(aborting.audio().size() < site.audio().size(),
                  L"an abort stops the utterance early");
        } else if (site.audio().empty()) {
            ++g_failures;
            wprintf(L"    ^ FAILED: no audio\n");
        }

        if (!out.empty() && format) {
            write_wav(out, site.audio(), *format);
            wprintf(L"  wrote %s\n", out.c_str());
        }
        if (format) {
            CoTaskMemFree(format);
        }
        engine->Release();
        token->Release();
    }

    tokens->Release();
    CoUninitialize();

    wprintf(L"\n%s\n", g_failures ? L"THERE WERE FAILURES"
                                  : L"All SAPI 5 self-tests passed.");
    return g_failures ? 1 : 0;
}
