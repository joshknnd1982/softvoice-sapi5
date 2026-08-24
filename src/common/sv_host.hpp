#pragma once

// A client for svwebspeak-host.exe, the 32-bit process that owns the 1997
// SoftVoice engine.
//
// This is the whole reason there is no separate 32-bit surrogate in this
// package. The engine is a 32-bit DLL that creates a top-level window and is
// not thread-safe, so it has to live in its own process whichever
// architecture is asking - which means the 64-bit SAPI interface and the
// 32-bit one can be the same code, talking to the same helper, over the same
// loopback socket. The bestspeech wrapper this is modelled on needed a
// named-pipe server precisely because its 32-bit path ran in-process; here
// there is nothing to split.
//
// Nothing in this path touches the registry. svwebspeak-host.exe and
// SVctl32.DLL are patched to hand the SoftVoice registration number to the
// engine in-process, so SVRegister never opens a key.

// winsock2.h before windows.h, so SOCKET is the Winsock 2 one. The project
// defines WIN32_LEAN_AND_MEAN, which keeps windows.h from dragging in the
// original winsock.h and colliding with it.
#include <winsock2.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <windows.h>

namespace SoftVoice {

// Called with each block of PCM as it arrives. Return false to abandon the
// utterance; the host is told to stop and speak() returns early.
using AudioSink = std::function<bool(const void* data, std::size_t bytes)>;

// Polled while speak() is waiting. Return false to abandon the utterance.
// This is what lets an interruption be noticed promptly rather than at the
// end of whatever is being rendered.
using CancelCheck = std::function<bool()>;

// Why a host failed to start, in terms a person can act on.
struct HostError {
    int status = 0;             // the engine's own status code, if it got that far
    std::wstring message;       // ready to show or log
    [[nodiscard]] bool ok() const noexcept { return message.empty(); }
};

class Host {
public:
    ~Host();

    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    // Start a host, with the engine opened at `sample_rate`. Returns null on
    // failure and fills `error`.
    [[nodiscard]] static std::unique_ptr<Host> start(unsigned sample_rate,
                                                     HostError* error);

    // The format the engine actually opened with. The engine fixes this at
    // SVOpenSpeech, so it can only be changed by starting another host.
    [[nodiscard]] unsigned sample_rate() const noexcept { return engine_rate_; }
    [[nodiscard]] unsigned bits_per_sample() const noexcept { return engine_bits_; }

    // Bit mask of the languages the engine loaded: 1 English, 2 Spanish.
    [[nodiscard]] unsigned languages() const noexcept { return languages_; }

    // True while the helper is running and the socket is good.
    [[nodiscard]] bool alive() const noexcept;

    void set_param(unsigned short param, int value);

    // Render `text` and feed it to `sink`. Blocks until the utterance is
    // complete, the sink or `keep_going` refuses, or the host dies.
    //
    // "Complete" deliberately does not mean "the host said done". The host
    // sends the whole utterance as a single audio frame and then sits on a
    // fixed idle timer for about 400 ms before reporting completion - 0 ms of
    // it is CPU, and it will not begin the next utterance until it expires.
    // Waiting for it costs 400 ms of dead time on every single utterance,
    // which is most of the delay between pressing a key and hearing anything.
    //
    // So instead: once audio has arrived and a short quiet period passes with
    // nothing further, the utterance is treated as finished and CMD_STOP is
    // sent, which cancels the timer. Measured: 420 ms per utterance becomes
    // 39 ms, and throughput goes from 2.4 to 25.7 utterances a second. The
    // quiet period is what keeps this safe if the host ever does split an
    // utterance across frames - every frame restarts it.
    bool speak(const std::wstring& text, const AudioSink& sink,
               const CancelCheck& keep_going = nullptr);

    // Abandon whatever is being rendered. Safe to call from another thread.
    void stop();

private:
    Host() = default;

    struct Chunk {
        bool done = false;
        std::uint32_t utterance = 0;
        std::vector<char> pcm;
    };

    bool launch(unsigned sample_rate, HostError* error);
    void reader_loop();
    bool send(std::uint16_t command, const void* payload, std::size_t size);
    bool recv_exact(void* buffer, std::size_t size);
    void teardown();

    SOCKET socket_ = INVALID_SOCKET;
    HANDLE process_ = nullptr;
    HANDLE reader_ = nullptr;

    CRITICAL_SECTION send_lock_{};
    CRITICAL_SECTION queue_lock_{};
    HANDLE queue_event_ = nullptr;
    bool locks_ready_ = false;

    std::vector<Chunk> queue_;
    volatile LONG closing_ = 0;
    volatile LONG reader_failed_ = 0;

    std::uint32_t message_id_ = 0;
    std::uint32_t sequence_ = 0;
    std::uint32_t utterance_ = 0;

    unsigned engine_rate_ = 22050;
    unsigned engine_bits_ = 16;
    unsigned languages_ = 0;

    HANDLE init_event_ = nullptr;
    int init_status_ = 0;
    bool init_seen_ = false;
};

// Encode text the way the engine expects it: Windows-1252, which is what a
// 1997 Western speech engine's letter-to-sound rules were written against.
// Deliberately not the process ANSI code page - on a machine whose ACP is
// Shift-JIS or 1251 that would hand the engine bytes it cannot read, and the
// engine speaks only English and Spanish either way.
[[nodiscard]] std::string encode_for_engine(const std::wstring& text);

}  // namespace SoftVoice
