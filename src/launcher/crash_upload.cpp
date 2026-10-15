#include "launcher/crash_upload.h"

#include "core/log.h"
#include "launcher/https.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>

namespace mu::launcher {

namespace fs = std::filesystem;

namespace {

// What one request may carry, raw (web/server.py MOST_CRASH_BODY holds the base64 of both).
constexpr size_t kMostText = 1u << 20;
constexpr size_t kMostDump = 3u << 20;
constexpr int kMostAStart = 8;
constexpr auto kOldest = std::chrono::hours(24 * 30);
// Younger than this, a report may still be being written: a freeze's is taken over seconds.
constexpr auto kSettled = std::chrono::seconds(30);

// Two senders in one process (the launcher's start and its after-Play look) take turns.
std::mutex g_sending;

bool readAll(const fs::path& path, std::string& out, size_t most) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (out.size() <= most) return true;
    // The head says what happened and the tail has the log's last lines: the middle goes.
    const size_t half = most / 2;
    out = out.substr(0, half) + "\n[... cut to fit ...]\n" + out.substr(out.size() - half);
    return true;
}

std::string base64(const std::string& in) {
    static const char* abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const uint32_t n = (uint8_t(in[i]) << 16) | (uint8_t(in[i + 1]) << 8) | uint8_t(in[i + 2]);
        out += abc[(n >> 18) & 63];
        out += abc[(n >> 12) & 63];
        out += abc[(n >> 6) & 63];
        out += abc[n & 63];
    }
    if (i < in.size()) {
        const uint32_t n = (uint8_t(in[i]) << 16) | (i + 1 < in.size() ? uint8_t(in[i + 1]) << 8 : 0);
        out += abc[(n >> 18) & 63];
        out += abc[(n >> 12) & 63];
        out += i + 1 < in.size() ? abc[(n >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

// A JSON string of a short field: quotes, backslashes and control characters escaped.
std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (uint8_t(c) < 0x20) {
            char esc[8];
            std::snprintf(esc, sizeof esc, "\\u%04x", unsigned(uint8_t(c)));
            out += esc;
        } else {
            out += c;
        }
    }
    return out + "\"";
}

bool isReport(const fs::path& path) {
    const std::string name = path.filename().string();
    if (path.extension() != ".txt") return false;
    return name.find("crash-") != std::string::npos || name.find("hang-") != std::string::npos;
}

void mark(const fs::path& path, const char* as) {
    std::error_code ec;
    fs::rename(path, fs::path(path.string() + as), ec);
}

// One report and its dump. 1 landed, 0 to try again later, -1 refused for good.
int sendOne(const fs::path& text, const CrashFrom& from) {
    std::string report;
    if (!readAll(text, report, kMostText) || report.empty()) return -1;
    fs::path dump = text;
    dump.replace_extension(".dmp");
    std::string dumped;
    std::error_code ec;
    if (fs::is_regular_file(dump, ec)) {
        const auto size = fs::file_size(dump, ec);
        if (!ec && size <= kMostDump) readAll(dump, dumped, kMostDump);
    }
    std::string body = "{\"file\":" + quoted(text.filename().string()) + ",\"platform\":" + quoted(from.platform) +
                       ",\"sender\":" + quoted(from.sender);
    if (!from.gpu.empty()) body += ",\"gpu\":" + quoted(from.gpu);
    if (!from.driver.empty()) body += ",\"driver\":" + quoted(from.driver);
    body += ",\"text\":\"" + base64(report) + "\"";
    if (!dumped.empty()) body += ",\"dump\":\"" + base64(dumped) + "\"";
    body += "}";
    std::vector<std::string> headers = {"Content-Type: application/json"};
    if (!from.token.empty()) headers.push_back("Authorization: Bearer " + from.token);
    const Reply reply = request("POST", from.site + "/api/crash", body, headers);
    if (reply.status == 200) {
        mark(text, ".sent");
        if (!dumped.empty()) mark(dump, ".sent");
        core::logf("crash report %s sent", text.filename().string().c_str());
        return 1;
    }
    if (reply.status == 400 || reply.status == 413) {
        mark(text, ".refused");
        core::logError("crash report %s refused: HTTP %d", text.filename().string().c_str(), reply.status);
        return -1;
    }
    core::logError("crash report %s not sent: %s", text.filename().string().c_str(),
                   reply.status ? ("HTTP " + std::to_string(reply.status)).c_str() : reply.error.c_str());
    return 0;
}

}  // namespace

int sendCrashes(const std::vector<fs::path>& folders, const CrashFrom& from) {
    std::lock_guard<std::mutex> hold(g_sending);
    std::vector<fs::path> waiting;
    const auto now = fs::file_time_type::clock::now();
    for (const fs::path& folder : folders) {
        std::error_code ec;
        if (!fs::is_directory(folder, ec)) continue;
        for (const fs::directory_entry& entry : fs::directory_iterator(folder, ec)) {
            if (!entry.is_regular_file(ec) || !isReport(entry.path())) continue;
            const auto age = now - entry.last_write_time(ec);
            if (ec || age < kSettled || age > kOldest) continue;
            if (std::find(waiting.begin(), waiting.end(), entry.path()) == waiting.end())
                waiting.push_back(entry.path());
        }
    }
    // Oldest first: the stamp in every name sorts by time.
    std::sort(waiting.begin(), waiting.end(),
              [](const fs::path& a, const fs::path& b) { return a.filename() < b.filename(); });
    int landed = 0, tried = 0;
    for (const fs::path& one : waiting) {
        if (tried++ >= kMostAStart) break;
        const int sent = sendOne(one, from);
        if (sent > 0) ++landed;
        if (sent == 0) break;  // the site is not answering: the rest wait for the next start
    }
    return landed;
}

std::string ticketFromEnvironment() {
    const char* handed = std::getenv("MU2_TICKET");
    if (!handed) return {};
    const std::string said = handed;
    const bool shaped = said.size() == 32 + 1 + 64 && said[32] == ':' &&
                        said.find_first_not_of("0123456789abcdef:") == std::string::npos;
    return shaped ? said : std::string();
}

}  // namespace mu::launcher
