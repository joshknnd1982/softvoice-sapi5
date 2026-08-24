#include "sv_host.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstring>

#include "sv_log.hpp"
#include "sv_paths.hpp"

#pragma comment(lib, "ws2_32.lib")

namespace SoftVoice {

namespace {

// Command numbers, as the host decodes them.
constexpr std::uint16_t kCmdSpeak = 1;
constexpr std::uint16_t kCmdStop = 2;
constexpr std::uint16_t kCmdParam = 3;
constexpr std::uint16_t kCmdShutdown = 4;

// Frame kinds the host sends back.
constexpr unsigned char kKindStatus = 2;
constexpr unsigned char kKindEvent = 3;

constexpr std::uint16_t kEventAudio = 1;
constexpr std::uint16_t kEventDone = 2;

constexpr DWORD kConnectTimeoutMs = 15000;
constexpr DWORD kInitTimeoutMs = 30000;

// How long to wait after the last audio frame before declaring the utterance
// finished. The host delivers a whole utterance in one frame - verified from
// 45 KB up to 7.8 MB, about three minutes of speech - and frames are
// length-prefixed, so a partial one cannot be mistaken for a whole one. This
// is therefore just the cost of being certain no second frame is coming, and
// it is the entire price paid for skipping the host's 400 ms idle timer.
//
// Measured against a clock that can actually see it: GetTickCount only moves
// every 15.6 ms, which at this scale is most of the budget.
constexpr double kQuietPeriodMs = 10.0;

// How often to look for an interruption while waiting. The wait itself is
// subject to the same 15.6 ms scheduler granularity, so asking for less than
// that buys nothing but does no harm either.
constexpr DWORD kPollMs = 5;

// A whole utterance must arrive within this; a wedged host must not hold the
// caller's speech thread for good.
constexpr DWORD kRenderTimeoutMs = 30000;

// Winsock is started once per process and never stopped: unloading the DLL
// mid-speech would otherwise pull the sockets out from under a reader
// thread. It cannot be done from DllMain - WSAStartup loads providers, which
// takes the loader lock - so it happens on the first attempt to start a host.
INIT_ONCE g_winsock_once = INIT_ONCE_STATIC_INIT;
bool g_winsock_ok = false;

BOOL CALLBACK start_winsock(PINIT_ONCE, PVOID, PVOID*)
{
    WSADATA data = {};
    g_winsock_ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    if (!g_winsock_ok) {
        SV_LOG_ERROR("host: WSAStartup failed, error %d", WSAGetLastError());
    }
    return TRUE;
}

std::wstring quote(const std::wstring& text)
{
    return L"\"" + text + L"\"";
}

// Milliseconds since some fixed point, with better resolution than the 15.6 ms
// GetTickCount offers. The quiet period below is only 10 ms, so measuring it
// with a 15.6 ms clock would systematically overshoot.
double now_ms()
{
    static LARGE_INTEGER frequency = [] {
        LARGE_INTEGER f = {};
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER counter = {};
    QueryPerformanceCounter(&counter);
    if (frequency.QuadPart == 0) {
        return static_cast<double>(GetTickCount());
    }
    return 1000.0 * static_cast<double>(counter.QuadPart) /
           static_cast<double>(frequency.QuadPart);
}

}  // namespace

std::string encode_for_engine(const std::wstring& text)
{
    if (text.empty()) {
        return std::string();
    }
    const int needed =
        WideCharToMultiByte(1252, 0, text.c_str(),
                            static_cast<int>(text.size()), nullptr, 0,
                            nullptr, nullptr);
    if (needed <= 0) {
        return std::string();
    }
    std::string result(static_cast<std::size_t>(needed), '\0');
    // A default of '?' rather than dropping the character, so an unspeakable
    // symbol still occupies a position and the text does not silently shift.
    const char fallback = '?';
    BOOL used = FALSE;
    WideCharToMultiByte(1252, 0, text.c_str(), static_cast<int>(text.size()),
                        result.data(), needed, &fallback, &used);
    return result;
}

Host::~Host()
{
    teardown();
}

bool Host::alive() const noexcept
{
    if (socket_ == INVALID_SOCKET || reader_failed_) {
        return false;
    }
    if (process_ && WaitForSingleObject(process_, 0) == WAIT_OBJECT_0) {
        return false;
    }
    return true;
}

std::unique_ptr<Host> Host::start(unsigned sample_rate, HostError* error)
{
    HostError local;
    if (!error) {
        error = &local;
    }
    *error = HostError();

    std::unique_ptr<Host> host(new Host());
    InitializeCriticalSection(&host->send_lock_);
    InitializeCriticalSection(&host->queue_lock_);
    host->locks_ready_ = true;
    host->queue_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    host->init_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!host->queue_event_ || !host->init_event_) {
        error->message = L"Could not create the events the speech host needs.";
        return nullptr;
    }

    if (!host->launch(sample_rate, error)) {
        return nullptr;
    }
    return host;
}

bool Host::launch(unsigned sample_rate, HostError* error)
{
    InitOnceExecuteOnce(&g_winsock_once, start_winsock, nullptr, nullptr);
    if (!g_winsock_ok) {
        error->message = L"Windows networking could not be started, so the "
                         L"speech host cannot be reached.";
        return false;
    }

    const std::wstring exe = paths::host_exe();
    if (exe.empty()) {
        error->message =
            L"svwebspeak-host.exe was not found next to the SAPI 5 interface. "
            L"The SoftVoice engine cannot start without it.";
        SV_LOG_ERROR("host: svwebspeak-host.exe not found (module dir %S)",
                     paths::module_dir().c_str());
        return false;
    }
    const std::wstring engine = paths::engine_dir();

    // The engine DLLs must be beside the host; say so plainly rather than
    // letting the engine fail with a bare status code.
    for (const wchar_t* dll : {L"SVctl32.DLL", L"SVENG32.DLL"}) {
        if (!paths::file_exists(engine + L"\\" + dll)) {
            error->message = std::wstring(dll) +
                             L" is missing from the SoftVoice program folder. "
                             L"Reinstall to restore it.";
            SV_LOG_ERROR("host: %S missing from %S", dll, engine.c_str());
            return false;
        }
    }
    if (!paths::file_exists(engine + L"\\Svspan32.dll")) {
        SV_LOG_WARN("host: Svspan32.dll missing from %S; the Spanish voices "
                    "will not be available",
                    engine.c_str());
    }

    // Listen first, then hand the host the port: it connects back to us, so
    // there is no fixed port and no clash between concurrent hosts.
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        error->message = L"Could not open a local socket for the speech host.";
        return false;
    }
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    int address_len = sizeof(address);
    if (bind(listener, reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) != 0 ||
        listen(listener, 1) != 0 ||
        getsockname(listener, reinterpret_cast<sockaddr*>(&address),
                    &address_len) != 0) {
        closesocket(listener);
        error->message = L"Could not listen on a local socket for the speech "
                         L"host.";
        return false;
    }
    const unsigned short port = ntohs(address.sin_port);

    wchar_t command[1024] = {};
    swprintf_s(command,
               L"%s --address 127.0.0.1:%u --dir %s --rate %u --bits 16",
               quote(exe).c_str(), static_cast<unsigned>(port),
               quote(engine).c_str(), sample_rate);
    SV_LOG_INFO("host: launching %S", command);

    STARTUPINFOW startup = {sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION info = {};
    // CREATE_NO_WINDOW so starting the engine never allocates a console or
    // briefly steals focus from whatever the user is doing.
    if (!CreateProcessW(nullptr, command, nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, engine.c_str(), &startup,
                        &info)) {
        const DWORD last = GetLastError();
        closesocket(listener);
        error->message = L"svwebspeak-host.exe could not be started.";
        SV_LOG_ERROR("host: CreateProcess failed, error %lu", last);
        return false;
    }
    CloseHandle(info.hThread);
    process_ = info.hProcess;

    // Bounded accept, so a host that dies on startup fails fast instead of
    // hanging whatever asked to speak.
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(listener, &readable);
    timeval timeout = {static_cast<long>(kConnectTimeoutMs / 1000),
                       static_cast<long>((kConnectTimeoutMs % 1000) * 1000)};
    const int ready = select(0, &readable, nullptr, nullptr, &timeout);
    if (ready <= 0) {
        closesocket(listener);
        error->message = L"The SoftVoice speech host started but never "
                         L"connected back.";
        SV_LOG_ERROR("host: no connection within %lu ms", kConnectTimeoutMs);
        teardown();
        return false;
    }
    socket_ = accept(listener, nullptr, nullptr);
    closesocket(listener);
    if (socket_ != INVALID_SOCKET) {
        // Parameter frames are tiny and are sent in bursts of a dozen before
        // the speak that depends on them. Left to Nagle they would be
        // coalesced and held for an ACK that a one-way stream never prompts.
        BOOL nodelay = TRUE;
        setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));
    }
    if (socket_ == INVALID_SOCKET) {
        error->message = L"The connection from the SoftVoice speech host "
                         L"could not be accepted.";
        teardown();
        return false;
    }

    // The reader is what delivers audio and the init status; without it the
    // client loads happily and is then permanently silent.
    reader_ = CreateThread(
        nullptr, 0,
        [](LPVOID self) -> DWORD {
            static_cast<Host*>(self)->reader_loop();
            return 0;
        },
        this, 0, nullptr);
    if (!reader_) {
        error->message = L"Could not start the thread that reads speech audio.";
        teardown();
        return false;
    }

    if (WaitForSingleObject(init_event_, kInitTimeoutMs) != WAIT_OBJECT_0 ||
        !init_seen_) {
        error->message = L"The SoftVoice engine never reported that it was "
                         L"ready.";
        SV_LOG_ERROR("host: no init status within %lu ms", kInitTimeoutMs);
        teardown();
        return false;
    }
    if (init_status_ != 0) {
        error->status = init_status_;
        if (init_status_ == 7025) {
            error->message =
                L"The SoftVoice engine reports that it is not registered "
                L"(status 7025). The shipped SVctl32.DLL and "
                L"svwebspeak-host.exe register the engine in-process, so this "
                L"means one of them has been replaced with an unpatched copy.";
        } else if (init_status_ == -4) {
            error->message =
                L"The SoftVoice engine could not find its registration "
                L"(status -4). The shipped binaries supply it without the "
                L"registry, so this means an unpatched copy is installed.";
        } else {
            wchar_t text[256];
            swprintf_s(text,
                       L"The SoftVoice engine failed to start (status %d). "
                       L"Check that SVctl32.DLL and SVENG32.DLL are present "
                       L"in the program folder.",
                       init_status_);
            error->message = text;
        }
        SV_LOG_ERROR("host: engine init failed, status %d", init_status_);
        teardown();
        return false;
    }

    SV_LOG_INFO("host: engine ready, %u Hz, %u bit, languages 0x%x",
                engine_rate_, engine_bits_, languages_);
    return true;
}

bool Host::recv_exact(void* buffer, std::size_t size)
{
    auto* out = static_cast<char*>(buffer);
    std::size_t got = 0;
    while (got < size) {
        const int n = recv(socket_, out + got,
                           static_cast<int>(size - got), 0);
        if (n <= 0) {
            return false;
        }
        got += static_cast<std::size_t>(n);
    }
    return true;
}

void Host::reader_loop()
{
    std::vector<char> frame;
    while (true) {
        std::uint32_t length = 0;
        if (!recv_exact(&length, sizeof(length)) || length == 0 ||
            length > (64u << 20)) {
            break;
        }
        frame.resize(length);
        if (!recv_exact(frame.data(), length)) {
            break;
        }

        const auto kind = static_cast<unsigned char>(frame[0]);
        if (kind == kKindStatus && length >= 9) {
            std::uint32_t message_id = 0;
            std::memcpy(&message_id, frame.data() + 1, 4);
            // Message id zero is the engine's own startup report, which is
            // the only status that carries the format and language mask.
            if (message_id == 0 && length >= 25) {
                std::int32_t rc = 0;
                std::uint32_t rate = 0, bits = 0, langs = 0;
                std::memcpy(&rc, frame.data() + 9, 4);
                std::memcpy(&rate, frame.data() + 13, 4);
                std::memcpy(&bits, frame.data() + 17, 4);
                std::memcpy(&langs, frame.data() + 21, 4);
                init_status_ = rc;
                engine_rate_ = rate;
                engine_bits_ = bits;
                languages_ = langs;
                init_seen_ = true;
                SetEvent(init_event_);
            }
            continue;
        }
        if (kind != kKindEvent || length < 3) {
            continue;
        }

        std::uint16_t event = 0;
        std::memcpy(&event, frame.data() + 1, 2);
        if (event == kEventAudio && length >= 11) {
            std::uint32_t seq = 0, bytes = 0;
            std::memcpy(&seq, frame.data() + 3, 4);
            std::memcpy(&bytes, frame.data() + 7, 4);
            if (seq != sequence_ || bytes == 0 || 11 + bytes > length) {
                continue;
            }
            Chunk chunk;
            chunk.pcm.assign(frame.begin() + 11,
                             frame.begin() + 11 + static_cast<long>(bytes));
            EnterCriticalSection(&queue_lock_);
            queue_.push_back(std::move(chunk));
            SetEvent(queue_event_);
            LeaveCriticalSection(&queue_lock_);
        } else if (event == kEventDone && length >= 11) {
            std::uint32_t seq = 0, utterance = 0;
            std::memcpy(&seq, frame.data() + 3, 4);
            std::memcpy(&utterance, frame.data() + 7, 4);
            if (seq != sequence_) {
                continue;
            }
            Chunk chunk;
            chunk.done = true;
            chunk.utterance = utterance;
            EnterCriticalSection(&queue_lock_);
            queue_.push_back(std::move(chunk));
            SetEvent(queue_event_);
            LeaveCriticalSection(&queue_lock_);
        }
    }

    // Wake anyone waiting, or a speak() in progress would block until its
    // own timeout rather than reporting the host as gone.
    InterlockedExchange(&reader_failed_, 1);
    EnterCriticalSection(&queue_lock_);
    SetEvent(queue_event_);
    LeaveCriticalSection(&queue_lock_);
    SetEvent(init_event_);
    if (!closing_) {
        SV_LOG_WARN("host: connection to the speech host closed");
    }
}

bool Host::send(std::uint16_t command, const void* payload, std::size_t size)
{
    if (socket_ == INVALID_SOCKET) {
        return false;
    }
    std::vector<char> frame(4 + 1 + 4 + 2 + size);
    const auto body_length = static_cast<std::uint32_t>(1 + 4 + 2 + size);
    std::memcpy(frame.data(), &body_length, 4);
    frame[4] = 1;  // request

    EnterCriticalSection(&send_lock_);
    const std::uint32_t id = ++message_id_;
    std::memcpy(frame.data() + 5, &id, 4);
    std::memcpy(frame.data() + 9, &command, 2);
    if (payload && size) {
        std::memcpy(frame.data() + 11, payload, size);
    }

    bool ok = true;
    std::size_t sent = 0;
    while (sent < frame.size()) {
        const int n = ::send(socket_, frame.data() + sent,
                             static_cast<int>(frame.size() - sent), 0);
        if (n <= 0) {
            ok = false;
            break;
        }
        sent += static_cast<std::size_t>(n);
    }
    LeaveCriticalSection(&send_lock_);

    if (!ok && !closing_) {
        SV_LOG_ERROR("host: send of command %u failed, error %d", command,
                     WSAGetLastError());
    }
    return ok;
}

void Host::set_param(unsigned short param, int value)
{
#pragma pack(push, 1)
    struct {
        std::uint16_t param;
        std::int32_t value;
    } payload{param, value};
#pragma pack(pop)
    static_assert(sizeof(payload) == 6, "the host expects a packed u16 + i32");
    SV_LOG_TRACE("host: param %u = %d", param, value);
    send(kCmdParam, &payload, sizeof(payload));
}

void Host::stop()
{
    // Bumping the sequence is what invalidates audio already in flight: the
    // reader drops anything not tagged with the current one, so a block that
    // was on the wire when the stop was issued is never handed to the sink.
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&sequence_));
    send(kCmdStop, nullptr, 0);

    EnterCriticalSection(&queue_lock_);
    queue_.clear();
    SetEvent(queue_event_);
    LeaveCriticalSection(&queue_lock_);
}

bool Host::speak(const std::wstring& text, const AudioSink& sink,
                 const CancelCheck& keep_going)
{
    const std::string encoded = encode_for_engine(text);
    if (encoded.empty()) {
        return true;
    }

    const std::uint32_t sequence = sequence_;
    const std::uint32_t utterance = ++utterance_;

    std::vector<char> payload(12 + encoded.size());
    std::memcpy(payload.data(), &sequence, 4);
    std::memcpy(payload.data() + 4, &utterance, 4);
    const auto length = static_cast<std::uint32_t>(encoded.size());
    std::memcpy(payload.data() + 8, &length, 4);
    std::memcpy(payload.data() + 12, encoded.data(), encoded.size());

    EnterCriticalSection(&queue_lock_);
    queue_.clear();
    ResetEvent(queue_event_);
    LeaveCriticalSection(&queue_lock_);

    SV_LOG_DEBUG("host: speak utterance %u, %u bytes", utterance, length);
    if (!send(kCmdSpeak, payload.data(), payload.size())) {
        return false;
    }

    std::size_t delivered = 0;
    const double started = now_ms();
    bool had_audio = false;
    double last_audio = 0.0;   // when the most recent frame arrived

    while (true) {
        if (reader_failed_) {
            SV_LOG_ERROR("host: the speech host stopped while rendering "
                         "utterance %u",
                         utterance);
            return false;
        }
        if (keep_going && !keep_going()) {
            SV_LOG_DEBUG("host: interrupted during utterance %u after "
                         "%zu bytes",
                         utterance, delivered);
            stop();
            return true;
        }

        std::vector<Chunk> batch;
        EnterCriticalSection(&queue_lock_);
        batch.swap(queue_);
        if (batch.empty()) {
            ResetEvent(queue_event_);
        }
        LeaveCriticalSection(&queue_lock_);

        if (batch.empty()) {
            if (had_audio && now_ms() - last_audio >= kQuietPeriodMs) {
                // Everything has arrived. Cancel the host's idle timer so it
                // is ready for the next utterance immediately instead of in
                // 400 ms; it will not report this one done afterwards, which
                // is exactly what we want.
                SV_LOG_DEBUG("host: utterance %u complete, %zu bytes in %.1f ms",
                             utterance, delivered, now_ms() - started);
                send(kCmdStop, nullptr, 0);
                return true;
            }
            if (now_ms() - started >= kRenderTimeoutMs) {
                SV_LOG_ERROR("host: no audio for %lu ms; giving up on "
                             "utterance %u",
                             kRenderTimeoutMs, utterance);
                return false;
            }
            // Short waits rather than one long one, so an interruption is
            // noticed within kPollMs instead of whenever audio next appears.
            WaitForSingleObject(queue_event_, kPollMs);
            continue;
        }

        for (const Chunk& chunk : batch) {
            if (chunk.done) {
                // Only seen when the host beat the quiet period to it.
                if (chunk.utterance != utterance) {
                    continue;
                }
                SV_LOG_DEBUG("host: utterance %u reported done, %zu bytes",
                             utterance, delivered);
                return true;
            }
            if (sequence != sequence_) {
                return true;  // superseded by a stop; not a failure
            }
            had_audio = true;
            last_audio = now_ms();
            delivered += chunk.pcm.size();
            if (sink && !sink(chunk.pcm.data(), chunk.pcm.size())) {
                SV_LOG_DEBUG("host: caller abandoned utterance %u after "
                             "%zu bytes",
                             utterance, delivered);
                stop();
                return true;
            }
        }
    }
}

void Host::teardown()
{
    // Orderly shutdown is a courtesy rather than a requirement. Measured:
    // the host exits by itself within half a second of its socket closing,
    // and Windows closes that handle when a process dies however it dies -
    // so an application that crashes mid-utterance leaves no orphaned
    // svwebspeak-host.exe behind.
    InterlockedExchange(&closing_, 1);

    if (socket_ != INVALID_SOCKET) {
        send(kCmdShutdown, nullptr, 0);
        // Shut the read side down so the reader thread's recv returns
        // instead of blocking on a host that is still tidying up.
        shutdown(socket_, SD_BOTH);
    }
    if (reader_) {
        if (WaitForSingleObject(reader_, 3000) != WAIT_OBJECT_0) {
            SV_LOG_WARN("host: reader thread did not exit; abandoning it");
        }
        CloseHandle(reader_);
        reader_ = nullptr;
    }
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    if (process_) {
        if (WaitForSingleObject(process_, 3000) != WAIT_OBJECT_0) {
            TerminateProcess(process_, 0);
        }
        CloseHandle(process_);
        process_ = nullptr;
    }
    if (queue_event_) {
        CloseHandle(queue_event_);
        queue_event_ = nullptr;
    }
    if (init_event_) {
        CloseHandle(init_event_);
        init_event_ = nullptr;
    }
    if (locks_ready_) {
        DeleteCriticalSection(&send_lock_);
        DeleteCriticalSection(&queue_lock_);
        locks_ready_ = false;
    }
}

}  // namespace SoftVoice
