#include "sv_engine.hpp"

#include <algorithm>
#include <cwctype>
#include <new>
#include <string>
#include <vector>

#include "sv_engine_params.hpp"
#include "sv_log.hpp"
#include "sv_utils.hpp"
#include "sv_voices.hpp"

namespace SoftVoice {
namespace sapi {

namespace {

constexpr WORD kChannels = 1;
constexpr WORD kBitsPerSample = 16;

// What the engine is asked to say when an application hands it nothing but
// punctuation - never sent, but keeps the empty case explicit.
struct SpeakContext {
    ISpTTSEngineSite* site = nullptr;
    ULONGLONG written = 0;
    bool aborted = false;
};

// One word found in a fragment, for the boundary events.
struct WordSpan {
    ULONG offset;  // characters from the start of the fragment
    ULONG length;
};

bool is_word_char(wchar_t c)
{
    return iswalnum(c) || c == L'\'' || c == L'-';
}

std::vector<WordSpan> find_words(const wchar_t* text, ULONG length)
{
    std::vector<WordSpan> words;
    bool inside = false;
    ULONG start = 0;
    for (ULONG i = 0; i <= length; ++i) {
        const bool word = i < length && is_word_char(text[i]);
        if (word && !inside) {
            start = i;
            inside = true;
        } else if (!word && inside) {
            words.push_back({start, i - start});
            inside = false;
        }
    }
    return words;
}

// True when the caller wants us to stop. Checked between blocks rather than
// only between fragments, because a fragment can be a long sentence and a
// screen reader user pressing a key expects speech to stop at once.
bool check_actions(SpeakContext* context)
{
    const DWORD actions = context->site->GetActions();
    if (actions & SPVES_ABORT) {
        context->aborted = true;
        return true;
    }
    if (actions & SPVES_SKIP) {
        context->site->CompleteSkip(0);
        context->aborted = true;
        return true;
    }
    return false;
}

// How much is offered to SAPI in one Write. This is what bounds how long an
// abort takes to be noticed, and it has to be bounded here because the host
// hands back a whole utterance in a single block - eight seconds of speech
// arrives as one 354 KB chunk, so without slicing there would be exactly one
// opportunity to stop per utterance, which for a screen reader means a
// keypress that appears to do nothing. 4096 bytes is about 93 ms at 22050 Hz.
constexpr ULONG kWriteSlice = 4096;

// Hand one block to SAPI, looping because Write may take less than offered
// and because the block is deliberately not offered all at once.
bool write_audio(SpeakContext* context, const void* data, std::size_t bytes)
{
    auto* pointer = static_cast<const BYTE*>(data);
    ULONG remaining = static_cast<ULONG>(bytes);
    while (remaining > 0) {
        if (check_actions(context)) {
            return false;
        }
        const ULONG slice = remaining < kWriteSlice ? remaining : kWriteSlice;
        ULONG written = 0;
        const HRESULT hr = context->site->Write(pointer, slice, &written);
        if (FAILED(hr)) {
            SV_LOG_ERROR("speak: ISpTTSEngineSite::Write failed, hr 0x%08lX",
                         static_cast<unsigned long>(hr));
            return false;
        }
        if (written == 0 || written > slice) {
            SV_LOG_ERROR("speak: Write reported %lu of %lu bytes", written,
                         slice);
            return false;
        }
        context->written += written;
        remaining -= written;
        pointer += written;
    }
    return true;
}

void add_event(SpeakContext* context, SPEVENTENUM id, ULONGLONG offset,
               WPARAM wparam, LPARAM lparam, SPEVENTLPARAMTYPE type)
{
    SPEVENT event = {};
    // eEventId and elParamType are 16-bit bitfields of their own enum type,
    // so they take the enum value and not a widened WORD.
    event.eEventId = id;
    event.elParamType = type;
    event.ullAudioStreamOffset = offset;
    event.ulStreamNum = 0;
    event.wParam = wparam;
    event.lParam = lparam;
    context->site->AddEvents(&event, 1);
}

}  // namespace

ISpTTSEngineImpl::ISpTTSEngineImpl()
{
    settings_.refresh();
    const Settings current = settings_.snapshot();
    stream_rate_ = current.sample_rate;
    SV_LOG_DEBUG("engine: created (sample rate %u)", stream_rate_);
}

ISpTTSEngineImpl::~ISpTTSEngineImpl()
{
    synth_.close();
}

STDMETHODIMP ISpTTSEngineImpl::SetObjectToken(ISpObjectToken* token)
{
    if (!token) {
        return E_INVALIDARG;
    }

    try {
        ISpDataKeyPtr attributes;
        if (FAILED(token->OpenKey(L"Attributes", &attributes))) {
            SV_LOG_ERROR("engine: the token has no Attributes key");
            return E_INVALIDARG;
        }

        utils::out_ptr<wchar_t> name(CoTaskMemFree);
        if (FAILED(attributes->GetStringValue(L"Name", name.address()))) {
            SV_LOG_ERROR("engine: the token has no Name attribute");
            return E_INVALIDARG;
        }

        const int resolved = voice_id_from_name(name.get());
        if (resolved < 0) {
            // Not fatal: speak in the first voice rather than refusing, so a
            // stale token in an application's settings still produces speech.
            SV_LOG_WARN("engine: unknown voice \"%S\"; using %S", name.get(),
                        Voice(0).name().c_str());
            voice_id_ = 0;
        } else {
            voice_id_ = resolved;
        }

        token_ = token;
        SV_LOG_INFO("engine: voice set to %S (id %d)",
                    Voice(static_cast<std::size_t>(voice_id_)).name().c_str(),
                    voice_id_);

        // Start the engine now rather than on the first thing spoken. It
        // costs about 35 ms, and paying it while a voice is being chosen is
        // free where paying it on the first utterance is heard as a stutter.
        // A failure is not reported: the voice must still be selectable, and
        // Speak will try again and report properly if it still cannot start.
        settings_.refresh();
        stream_rate_ = settings_.snapshot().sample_rate;
        HostError error;
        if (!synth_.ensure_host(stream_rate_, &error)) {
            SV_LOG_WARN("engine: could not pre-start the host (%S); "
                        "will retry when asked to speak",
                        error.message.c_str());
        }
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}

STDMETHODIMP ISpTTSEngineImpl::GetObjectToken(ISpObjectToken** token)
{
    if (!token) {
        return E_POINTER;
    }
    *token = nullptr;
    if (!token_) {
        return E_UNEXPECTED;
    }
    token_.AddRef();
    *token = token_.GetInterfacePtr();
    return S_OK;
}

STDMETHODIMP ISpTTSEngineImpl::GetOutputFormat(const GUID*,
                                               const WAVEFORMATEX*,
                                               GUID* output_id,
                                               WAVEFORMATEX** output_format)
{
    if (!output_id || !output_format) {
        return E_POINTER;
    }
    *output_format = nullptr;
    *output_id = SPDFID_WaveFormatEx;

    // Latched here rather than read at Speak time: SAPI fixes the stream
    // format from this answer, so the host must later be opened at exactly
    // the rate reported now or every utterance would play at the wrong
    // speed.
    settings_.refresh();
    stream_rate_ = settings_.snapshot().sample_rate;

    auto* format =
        static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
    if (!format) {
        return E_OUTOFMEMORY;
    }
    format->wFormatTag = WAVE_FORMAT_PCM;
    format->nChannels = kChannels;
    format->nSamplesPerSec = stream_rate_;
    format->wBitsPerSample = kBitsPerSample;
    format->nBlockAlign = format->nChannels * format->wBitsPerSample / 8;
    format->nAvgBytesPerSec = format->nSamplesPerSec * format->nBlockAlign;
    format->cbSize = 0;

    *output_format = format;
    SV_LOG_DEBUG("engine: output format %lu Hz, %u bit, %u channel",
                 format->nSamplesPerSec, format->wBitsPerSample,
                 format->nChannels);
    return S_OK;
}

STDMETHODIMP ISpTTSEngineImpl::Speak(DWORD flags, REFGUID,
                                     const WAVEFORMATEX*,
                                     const SPVTEXTFRAG* fragments,
                                     ISpTTSEngineSite* site)
{
    if (!fragments || !site) {
        return E_INVALIDARG;
    }

    try {
        // Picking up an edit made in the configuration utility costs one
        // file stat per utterance, which is what makes a change there take
        // effect on the next thing spoken without restarting anything.
        settings_.refresh();
        const Settings current = settings_.snapshot();

        HostError error;
        if (!synth_.ensure_host(stream_rate_, &error)) {
            SV_LOG_ERROR("speak: no engine available - %S",
                         error.message.c_str());
            return E_FAIL;
        }

        const Voice voice(static_cast<std::size_t>(voice_id_));
        const VoiceSettings stored = current.for_voice(voice.name());

        ULONGLONG interest = 0;
        site->GetEventInterest(&interest);
        const bool want_sentences =
            (interest & SPFEI(SPEI_SENTENCE_BOUNDARY)) != 0;
        const bool want_words = (interest & SPFEI(SPEI_WORD_BOUNDARY)) != 0;

        long sapi_rate = 0;
        site->GetRate(&sapi_rate);
        USHORT sapi_volume = 100;
        site->GetVolume(&sapi_volume);

        SV_LOG_DEBUG("speak: flags 0x%08lX, voice %S, SAPI rate %+ld, "
                     "volume %u, events sentence=%d word=%d",
                     flags, voice.name().c_str(), sapi_rate, sapi_volume,
                     static_cast<int>(want_sentences),
                     static_cast<int>(want_words));

        SpeakContext context;
        context.site = site;

        for (const SPVTEXTFRAG* fragment = fragments; fragment;
             fragment = fragment->pNext) {
            if (check_actions(&context)) {
                break;
            }
            if (fragment->State.eAction == SPVA_Bookmark) {
                // Bookmarks are how a screen reader tracks its place, so
                // they matter more here than word boundaries do.
                std::wstring text;
                if (fragment->ulTextLen > 0 && fragment->pTextStart) {
                    text.assign(fragment->pTextStart, fragment->ulTextLen);
                }
                long id = 0;
                try {
                    id = std::stol(text);
                }
                catch (...) {
                    id = 0;
                }
                add_event(&context, SPEI_TTS_BOOKMARK, context.written, id,
                          reinterpret_cast<LPARAM>(text.c_str()),
                          SPET_LPARAM_IS_STRING);
                SV_LOG_TRACE("speak: bookmark \"%S\" at %llu", text.c_str(),
                             context.written);
                continue;
            }

            if (fragment->State.eAction == SPVA_Silence) {
                const ULONG ms = fragment->State.SilenceMSecs;
                if (ms == 0) {
                    continue;
                }
                const std::size_t bytes =
                    static_cast<std::size_t>(stream_rate_) * ms / 1000 * 2;
                const std::vector<char> silence(bytes, 0);
                if (!write_audio(&context, silence.data(), silence.size())) {
                    break;
                }
                continue;
            }

            if (fragment->State.eAction != SPVA_Speak &&
                fragment->State.eAction != SPVA_SpellOut &&
                fragment->State.eAction != SPVA_Pronounce) {
                continue;
            }
            if (fragment->ulTextLen == 0 || !fragment->pTextStart) {
                continue;
            }

            std::wstring text(fragment->pTextStart, fragment->ulTextLen);

            // SPVA_SpellOut asks for the text letter by letter. The engine
            // has no spell mode, so the letters are separated here; without
            // it "abc" would be pronounced as a word.
            if (fragment->State.eAction == SPVA_SpellOut) {
                std::wstring spelled;
                spelled.reserve(text.size() * 2);
                for (const wchar_t c : text) {
                    spelled += c;
                    spelled += L' ';
                }
                text.swap(spelled);
            }

            // The engine takes rate, pitch and volume for a whole call, so
            // the fragment's own adjustments are folded in before it starts.
            SpeechAdjust adjust;
            adjust.rate = std::clamp<int>(
                static_cast<int>(sapi_rate) + fragment->State.RateAdj, -10, 10);
            adjust.pitch =
                std::clamp<int>(fragment->State.PitchAdj.MiddleAdj, -10, 10);
            adjust.volume = std::clamp<int>(
                static_cast<int>(sapi_volume) *
                    static_cast<int>(fragment->State.Volume) / 100,
                0, 100);
            synth_.apply(voice_id_, stored, adjust);

            if (want_sentences) {
                add_event(&context, SPEI_SENTENCE_BOUNDARY, context.written,
                          fragment->ulTextLen, fragment->ulTextSrcOffset,
                          SPET_LPARAM_IS_UNDEFINED);
            }

            if (!want_words) {
                // The path a screen reader takes: audio is handed to SAPI as
                // it arrives, with no extra copy. In practice the host
                // returns the whole utterance in one block, so this is not
                // meaningfully earlier than the buffered path below - but it
                // does avoid holding a second copy of every utterance, and
                // it is what would stream if the host ever chunked.
                const bool ok = synth_.speak(
                    text,
                    [&](const void* data, std::size_t bytes) {
                        return write_audio(&context, data, bytes);
                    },
                    // Polled while the engine is waiting. Without it an
                    // interruption is only noticed when the next block of
                    // audio turns up, which is the difference between speech
                    // stopping when a key is pressed and stopping when the
                    // sentence happens to end.
                    [&] { return !check_actions(&context); });
                if (!ok && !context.aborted) {
                    SV_LOG_ERROR("speak: the engine failed to render a "
                                 "fragment");
                    return E_FAIL;
                }
                if (context.aborted) {
                    break;
                }
                continue;
            }

            // The client wants word boundaries. The engine reports no
            // timing of any kind, so the only way to place them is against
            // the finished audio - which means holding the fragment back
            // until it is rendered. Applications that highlight text ask for
            // these; screen readers do not, and take the path above.
            std::vector<char> rendered;
            const bool ok = synth_.speak(
                text,
                [&](const void* data, std::size_t bytes) {
                    const auto* start = static_cast<const char*>(data);
                    rendered.insert(rendered.end(), start, start + bytes);
                    return !check_actions(&context);
                },
                [&] { return !check_actions(&context); });
            if (context.aborted) {
                break;
            }
            if (!ok) {
                SV_LOG_ERROR("speak: the engine failed to render a fragment");
                return E_FAIL;
            }

            const std::vector<WordSpan> words =
                find_words(text.c_str(), static_cast<ULONG>(text.size()));
            const ULONGLONG base = context.written;
            const std::size_t total = rendered.size();
            std::size_t emitted = 0;
            bool broken = false;

            for (const WordSpan& word : words) {
                // Proportional to where the word sits in the text. It is an
                // estimate and is documented as one, but it tracks a
                // sentence far better than reporting every word at its
                // start, which makes a highlight jump to the end at once.
                std::size_t upto =
                    text.empty() ? 0
                                 : total * word.offset / text.size();
                upto -= upto % 2;  // never split a 16-bit sample
                upto = (std::min)(upto, total);
                if (upto > emitted) {
                    if (!write_audio(&context, rendered.data() + emitted,
                                     upto - emitted)) {
                        broken = true;
                        break;
                    }
                    emitted = upto;
                }
                add_event(&context, SPEI_WORD_BOUNDARY, base + emitted,
                          word.length,
                          fragment->ulTextSrcOffset + word.offset,
                          SPET_LPARAM_IS_UNDEFINED);
            }
            if (broken) {
                break;
            }
            if (emitted < total &&
                !write_audio(&context, rendered.data() + emitted,
                             total - emitted)) {
                break;
            }
        }

        if (context.aborted) {
            synth_.stop();
            SV_LOG_DEBUG("speak: stopped after %llu bytes", context.written);
        } else {
            SV_LOG_DEBUG("speak: finished, %llu bytes", context.written);
        }
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        SV_LOG_ERROR("speak: unexpected exception");
        return E_UNEXPECTED;
    }
}

}  // namespace sapi
}  // namespace SoftVoice
