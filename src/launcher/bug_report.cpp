#include "launcher/bug_report.h"

#include <cstdio>

#include "core/log.h"
#include "launcher/https.h"

namespace mu::launcher {

namespace {

// A JSON string: quotes, backslashes and control characters escaped (crash_upload.cpp's own).
std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
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

}  // namespace

bool sendBug(const std::string& site, const std::string& token, const BugReport& r, std::string* why) {
    std::string body = "{\"text\":" + quoted(r.text) + ",\"platform\":" + quoted(r.platform) +
                       ",\"build\":" + quoted(r.build) + ",\"world\":" + quoted(r.world) +
                       ",\"x\":" + std::to_string(r.x) + ",\"y\":" + std::to_string(r.y) +
                       ",\"level\":" + std::to_string(r.level);
    if (!r.character.empty()) body += ",\"character\":" + quoted(r.character);
    if (!r.kind.empty()) body += ",\"class\":" + quoted(r.kind);
    if (!r.renderer.empty()) body += ",\"renderer\":" + quoted(r.renderer);
    if (!r.gpu.empty()) body += ",\"gpu\":" + quoted(r.gpu);
    if (!r.server.empty()) body += ",\"server\":" + quoted(r.server);
    body += "}";
    std::vector<std::string> headers = {"Content-Type: application/json"};
    if (!token.empty()) headers.push_back("Authorization: Bearer " + token);
    const Reply reply = request("POST", site + "/api/bug", body, headers);
    if (reply.status == 200) {
        core::logf("bug report sent (%zu characters)", r.text.size());
        return true;
    }
    core::logError("bug report not sent: %s",
                   reply.status ? ("HTTP " + std::to_string(reply.status)).c_str() : reply.error.c_str());
    if (why) {
        *why = reply.status == 429 ? "Too many reports for now. Try again in a while."
             : reply.status == 400 || reply.status == 413 ? "The report was refused. Make it shorter."
             : reply.status ? "The website could not take it. Try again later."
                            : "Could not reach the website. Check your connection.";
    }
    return false;
}

}  // namespace mu::launcher
