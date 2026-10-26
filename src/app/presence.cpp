#include "app/presence.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#include "core/log.h"

namespace mu::app::presence {
namespace {

// MU Classic's application in the Discord Developer Portal.
constexpr const char* kApplication = "1564013551641698494";
constexpr const char* kSite = "https://mu-classic.com/";
constexpr int kGap = 15;    // seconds between two activities sent
constexpr int kRetry = 30;  // seconds between two tries at a socket that was not there

using Clock = std::chrono::steady_clock;

enum class Stage : uint8_t { None, Choosing, Playing };

// What the modes said last, and a count bumped each time it changed.
struct Said {
    Stage stage = Stage::None;
    std::string map, kinName;
    int32_t level = 0, rebirths = 0;
    int party = 0;
    uint64_t changes = 0;
};

std::mutex mutex;
std::condition_variable wake;
Said said;
bool running = false;
std::thread thread;
int64_t startedAt = 0;  // unix seconds: the "elapsed" Discord counts up from, the whole run's

// ---- the socket -----------------------------------------------------------------------------

#ifdef _WIN32
HANDLE pipe = INVALID_HANDLE_VALUE;

bool connected() { return pipe != INVALID_HANDLE_VALUE; }

bool open() {
    for (int i = 0; i < 10; ++i) {
        char name[64];
        std::snprintf(name, sizeof(name), "\\\\.\\pipe\\discord-ipc-%d", i);
        pipe = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) return true;
    }
    return false;
}

void close() {
    if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
    pipe = INVALID_HANDLE_VALUE;
}

bool write(const void* bytes, size_t length) {
    DWORD wrote = 0;
    return WriteFile(pipe, bytes, DWORD(length), &wrote, nullptr) && wrote == length;
}

// Up to `length` bytes, waiting at most `waitMs` for the first: 0 when nothing came, -1 when
// the pipe is gone.
int read(void* bytes, size_t length, int waitMs) {
    const auto until = Clock::now() + std::chrono::milliseconds(waitMs);
    for (;;) {
        DWORD ready = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &ready, nullptr)) return -1;
        if (ready > 0) {
            DWORD got = 0;
            if (!ReadFile(pipe, bytes, DWORD(length < ready ? length : ready), &got, nullptr)) return -1;
            return int(got);
        }
        if (Clock::now() >= until) return 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

int64_t ourPid() { return int64_t(GetCurrentProcessId()); }
#else
int sock = -1;

bool connected() { return sock >= 0; }

bool open() {
    // Discord's own order of places, and the Linux Flatpak's and Snap's below them.
    std::string base = "/tmp";
    for (const char* name : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"}) {
        if (const char* dir = std::getenv(name); dir && *dir) {
            base = dir;
            break;
        }
    }
    while (base.size() > 1 && base.back() == '/') base.pop_back();
    for (const char* under : {"", "/app/com.discordapp.Discord", "/snap.discord"}) {
        for (int i = 0; i < 10; ++i) {
            sockaddr_un address{};
            address.sun_family = AF_UNIX;
            std::snprintf(address.sun_path, sizeof(address.sun_path), "%s%s/discord-ipc-%d",
                          base.c_str(), under, i);
            sock = socket(AF_UNIX, SOCK_STREAM, 0);
            if (sock < 0) return false;
#ifdef SO_NOSIGPIPE
            const int on = 1;
            setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
            if (connect(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) return true;
            ::close(sock);
            sock = -1;
        }
    }
    return false;
}

void close() {
    if (sock >= 0) ::close(sock);
    sock = -1;
}

bool write(const void* bytes, size_t length) {
#ifdef MSG_NOSIGNAL
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    const char* at = static_cast<const char*>(bytes);
    while (length > 0) {
        const ssize_t sent = send(sock, at, length, flags);
        if (sent <= 0) return false;
        at += sent;
        length -= size_t(sent);
    }
    return true;
}

int read(void* bytes, size_t length, int waitMs) {
    pollfd one{sock, POLLIN, 0};
    const int ready = poll(&one, 1, waitMs);
    if (ready < 0) return -1;
    if (ready == 0) return 0;
    const ssize_t got = recv(sock, bytes, length, 0);
    return got > 0 ? int(got) : -1;
}

int64_t ourPid() { return int64_t(getpid()); }
#endif

// A frame: its opcode and length, little-endian, then the JSON. 0 is the handshake, 1 a command,
// 2 Discord closing.
bool send(uint32_t opcode, const std::string& json) {
    std::string frame(8 + json.size(), '\0');
    const uint32_t length = uint32_t(json.size());
    for (int i = 0; i < 4; ++i) {
        frame[i] = char((opcode >> (8 * i)) & 0xFF);
        frame[4 + i] = char((length >> (8 * i)) & 0xFF);
    }
    std::memcpy(frame.data() + 8, json.data(), json.size());
    return write(frame.data(), frame.size());
}

// Whatever Discord has said back, read and let go: its READY and its answers to our commands.
// False when the socket is gone or Discord closed it.
bool drain(int waitMs) {
    char bytes[4096];
    bool first = true;
    for (;;) {
        const int got = read(bytes, sizeof(bytes), first ? waitMs : 0);
        first = false;
        if (got < 0) return false;
        if (got == 0) return true;
        if (got >= 8 && bytes[0] == 2 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0) return false;
        // An activity Discord would not take says why, which is worth the log.
        const std::string text(bytes, size_t(got));
        if (text.find("\"evt\":\"ERROR\"") != std::string::npos) {
            core::logError("discord: %s", text.substr(8, 300).c_str());
        }
    }
}

bool handshake() {
    if (!open()) return false;
    std::string json = std::string("{\"v\":1,\"client_id\":\"") + kApplication + "\"}";
    if (!send(0, json) || !drain(3000)) {
        close();
        return false;
    }
    core::logf("discord: connected");
    return true;
}

// ---- the activity ---------------------------------------------------------------------------

// Not `quoted`: called on a std::string, argument lookup finds std::quoted too where the standard
// library declares it here (Linux's libstdc++), and it takes a non-const string better.
std::string jsonString(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (uint8_t(c) < 0x20) {
            char escaped[8];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", unsigned(uint8_t(c)));
            out += escaped;
        } else {
            out += c;
        }
    }
    return out + "\"";
}

std::string activity(const Said& now, uint64_t nonce) {
    std::string details, state;
    if (now.stage == Stage::Choosing) {
        details = "Choosing a hero";
    } else {
        details = now.map;
        state = now.kinName + " \xC2\xB7 Level " + std::to_string(now.level);
        if (now.rebirths > 0) state += " \xC2\xB7 Rebirth " + std::to_string(now.rebirths);
    }
    std::string json = "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"" + std::to_string(nonce) +
                       "\",\"args\":{\"pid\":" + std::to_string(ourPid()) + ",\"activity\":{";
    json += "\"details\":" + jsonString(details);
    if (!state.empty()) json += ",\"state\":" + jsonString(state);
    // A party shows as Discord's own "(2 of 5)" after the state.
    if (now.stage == Stage::Playing && now.party > 1) {
        json += ",\"party\":{\"id\":\"mu\",\"size\":[" + std::to_string(now.party) + "," +
                std::to_string(now.party > 5 ? now.party : 5) + "]}";
    }
    json += ",\"timestamps\":{\"start\":" + std::to_string(startedAt) + "}";
    json += ",\"assets\":{\"large_image\":\"logo\",\"large_text\":\"MU Classic\"}";
    json += ",\"buttons\":[{\"label\":\"Play MU Classic\",\"url\":\"" + std::string(kSite) + "\"}]";
    return json + "}}}";
}

void run() {
    uint64_t sentChanges = 0, nonce = 0;
    Clock::time_point nextTry{}, nextSend{};
    std::unique_lock<std::mutex> lock(mutex);
    while (running) {
        wake.wait_for(lock, std::chrono::seconds(1));
        if (!running) break;
        if (said.stage == Stage::None) continue;
        const Clock::time_point now = Clock::now();
        if (!connected()) {
            if (now < nextTry) continue;
            lock.unlock();
            const bool up = handshake();
            lock.lock();
            if (!up) {
                nextTry = now + std::chrono::seconds(kRetry);
                continue;
            }
            sentChanges = 0;  // a new socket has no activity yet
        }
        if (said.changes != sentChanges && now >= nextSend) {
            const Said copy = said;
            lock.unlock();
            const bool sent = send(1, activity(copy, ++nonce)) && drain(2000);
            lock.lock();
            if (sent) {
                sentChanges = copy.changes;
                nextSend = now + std::chrono::seconds(kGap);
            } else {
                core::logf("discord: the socket closed; trying again in %d s", kRetry);
                close();
                nextTry = now + std::chrono::seconds(kRetry);
            }
        } else if (!drain(0)) {
            close();
            nextTry = now + std::chrono::seconds(kRetry);
        }
    }
    lock.unlock();
    close();
}

}  // namespace

void start() {
    std::lock_guard<std::mutex> lock(mutex);
    if (running) return;
    if (const char* off = std::getenv("MU2_NO_DISCORD"); off && *off) return;
    startedAt = int64_t(std::time(nullptr));
    running = true;
    thread = std::thread(run);
}

void stop() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!running) return;
        running = false;
    }
    wake.notify_one();
    thread.join();
}

void choosing() {
    std::lock_guard<std::mutex> lock(mutex);
    if (said.stage == Stage::Choosing) return;
    said.stage = Stage::Choosing;
    ++said.changes;
    wake.notify_one();
}

void playing(const char* map, const char* kinName, int32_t level, int32_t rebirths, int party) {
    std::lock_guard<std::mutex> lock(mutex);
    if (said.stage == Stage::Playing && said.level == level && said.rebirths == rebirths &&
        said.party == party && said.map == map && said.kinName == kinName) {
        return;
    }
    said.stage = Stage::Playing;
    said.map = map;
    said.kinName = kinName;
    said.level = level;
    said.rebirths = rebirths;
    said.party = party;
    ++said.changes;
    wake.notify_one();
}

}  // namespace mu::app::presence
