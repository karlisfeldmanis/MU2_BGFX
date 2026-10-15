#include "launcher/engine.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

#include "core/args.h"
#include "core/json.h"
#include "core/log.h"
#include "launcher/gpu.h"
#include "launcher/https.h"
#include "launcher/sha256.h"

#ifdef _WIN32
#include <windows.h>
#include <dpapi.h>
#include <shlobj.h>
#else
#include <spawn.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>
extern char** environ;
#endif

namespace mu::launcher {

namespace {

std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += char(c);
        } else if (c < 0x20) {
            char esc[8];
            std::snprintf(esc, sizeof esc, "\\u%04x", c);
            out += esc;
        } else {
            out += char(c);
        }
    }
    return out + "\"";
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream all;
    all << in.rdbuf();
    return all.str();
}

bool writeFile(const fs::path& path, const std::string& bytes) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const fs::path part = path.string() + ".new";
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(bytes.data(), std::streamsize(bytes.size()));
        if (!out) return false;
    }
    fs::remove(path, ec);
    fs::rename(part, path, ec);
    return !ec;
}

core::Json parse(const std::string& text, const char* what) {
    return text.empty() ? core::Json{} : core::parseJson(text.data(), text.size(), what);
}

const char* kJson = "Content-Type: application/json";

// A file's time as its clock's count, in a string: past what a JSON number (a double) holds exactly.
std::string stamp(const fs::path& path) {
    std::error_code ec;
    return std::to_string(int64_t(fs::last_write_time(path, ec).time_since_epoch().count()));
}

#ifdef _WIN32
// ---- what a report says about the machine around the card ----------------------------------------

std::string wide(const wchar_t* text) {
    if (!text || !*text) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string out(size_t(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    return out;
}

// The name the CPU was sold under: HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\0.
std::string cpuName() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ,
                      &key) != ERROR_SUCCESS) {
        return {};
    }
    wchar_t name[96] = {};
    DWORD type = 0, size = sizeof name - sizeof(wchar_t);
    const bool got = RegQueryValueExW(key, L"ProcessorNameString", nullptr, &type,
                                      reinterpret_cast<LPBYTE>(name), &size) == ERROR_SUCCESS &&
                     type == REG_SZ;
    RegCloseKey(key);
    return got ? wide(name) : std::string{};
}

// All the memory the machine has, in MB.
int ramMb() {
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof memory;
    return GlobalMemoryStatusEx(&memory) ? int(memory.ullTotalPhys >> 20) : 0;
}

// Windows as its own kernel reports it: "10.0.26100". GetVersionEx would lie without a manifest;
// RtlGetVersion does not, and ntdll has had it in every Windows the game runs on.
std::string osVersion() {
    using RtlGetVersion = LONG(WINAPI*)(OSVERSIONINFOW*);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto rtl = ntdll ? reinterpret_cast<RtlGetVersion>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof version;
    if (rtl == nullptr || rtl(&version) != 0) return {};
    return std::to_string(version.dwMajorVersion) + "." + std::to_string(version.dwMinorVersion) + "." +
           std::to_string(version.dwBuildNumber);
}
#else
// The same three for a Mac (docs/plans/launcher.md, L5), from sysctl: the CPU's trade name, the
// memory the machine sells with, and the system's own version string ("14.5").
std::string sysctlText(const char* what) {
    char text[128] = {};
    size_t size = sizeof text - 1;
    return sysctlbyname(what, text, &size, nullptr, 0) == 0 ? std::string(text) : std::string{};
}

std::string cpuName() { return sysctlText("machdep.cpu.brand_string"); }

int ramMb() {
    uint64_t bytes = 0;
    size_t size = sizeof bytes;
    return sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) == 0 ? int(bytes >> 20) : 0;
}

std::string osVersion() { return sysctlText("kern.osproductversion"); }
#endif

}  // namespace

// ---- the account --------------------------------------------------------------------------------

Answer signIn(const Place& place, const std::string& email, const std::string& password, Signed& out,
              std::string& message) {
    const Reply reply = request("POST", place.site + "/api/login",
                                "{\"email\":" + quoted(email) + ",\"password\":" + quoted(password) + "}", {kJson});
    if (reply.status == 0) {
        core::logError("launcher: sign-in: %s", reply.error.c_str());
        message = "Can't reach the server. Check your connection.";
        return Answer::Offline;
    }
    const core::Json said = parse(reply.body, "the sign-in's answer");
    if (reply.status == 200) {
        out.token = said["token"].stringOr("");
        out.email = said["email"].stringOr(email.c_str());
        if (out.token.size() == 64) return Answer::Ok;
        message = "The server's answer made no sense. Try again.";
        return Answer::Offline;
    }
    message = said["error"].stringOr(reply.status == 429 ? "Too many tries. Wait a minute." : "Wrong email or password.");
    return reply.status == 429 ? Answer::Busy : Answer::Wrong;
}

Answer stillSigned(const Place& place, const Signed& who) {
    const Reply reply = request("GET", place.site + "/api/me", {}, {"Authorization: Bearer " + who.token});
    if (reply.status == 200) return Answer::Ok;
    if (reply.status == 401) return Answer::Wrong;
    return Answer::Offline;
}

Answer gameTicket(const Place& place, const Signed& who, GameTicket& out, std::string& message) {
    const Reply reply = request("POST", place.site + "/api/ticket", "{}", {kJson, "Authorization: Bearer " + who.token});
    if (reply.status == 0) {
        core::logError("launcher: ticket: %s", reply.error.c_str());
        message = "Can't reach the server. Check your connection.";
        return Answer::Offline;
    }
    const core::Json said = parse(reply.body, "the ticket's answer");
    if (reply.status == 200) {
        out.ticket = said["ticket"].stringOr("");
        out.secret = said["secret"].stringOr("");
        if (out.ticket.size() == 32 && out.secret.size() == 64) return Answer::Ok;
        message = "The server's answer made no sense. Try again.";
        return Answer::Offline;
    }
    if (reply.status == 401) return Answer::Wrong;
    message = said["error"].stringOr("The server would not start the game. Try again.");
    return reply.status == 429 ? Answer::Busy : Answer::Offline;
}

void signOut(const Place& place, const Signed& who) {
    if (!who.token.empty()) request("POST", place.site + "/api/logout", "{}", {kJson, "Authorization: Bearer " + who.token});
    forget();
}

// ---- the launcher's own files -------------------------------------------------------------------

#ifdef _WIN32
namespace {
fs::path known(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    fs::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path))) out = fs::path(path);
    CoTaskMemFree(path);
    return out;
}
}  // namespace

fs::path userFolder() { return known(FOLDERID_RoamingAppData) / "MU2"; }
fs::path defaultInstall() { return known(FOLDERID_LocalAppData) / "Programs" / "MU2"; }

bool keep(const Signed& who) {
    const std::string plain = who.token + "\n" + who.email;
    DATA_BLOB in{DWORD(plain.size()), (BYTE*)plain.data()}, out{};
    if (!CryptProtectData(&in, L"MU2 sign-in", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        core::logError("launcher: the sign-in could not be sealed (%lu)", GetLastError());
        return false;
    }
    const bool ok = writeFile(userFolder() / "session", std::string((const char*)out.pbData, out.cbData));
    LocalFree(out.pbData);
    return ok;
}

bool kept(Signed& who) {
    const std::string sealed = readFile(userFolder() / "session");
    if (sealed.empty()) return false;
    DATA_BLOB in{DWORD(sealed.size()), (BYTE*)sealed.data()}, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return false;
    const std::string plain((const char*)out.pbData, out.cbData);
    LocalFree(out.pbData);
    const size_t line = plain.find('\n');
    who.token = plain.substr(0, line);
    who.email = line == std::string::npos ? "" : plain.substr(line + 1);
    return who.token.size() == 64;
}
#else
fs::path userFolder() {
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : ".") / "Library" / "Application Support" / "MU2";
}
fs::path defaultInstall() {
    // ~/Applications, so the game's MU2.app lands where a Mac player looks for one (L5).
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : ".") / "Applications";
}

bool keep(const Signed& who) {
    const fs::path path = userFolder() / "session";
    if (!writeFile(path, who.token + "\n" + who.email)) return false;
    ::chmod(path.c_str(), 0600);
    return true;
}

bool kept(Signed& who) {
    const std::string plain = readFile(userFolder() / "session");
    const size_t line = plain.find('\n');
    who.token = plain.substr(0, line);
    who.email = line == std::string::npos ? "" : plain.substr(line + 1);
    return who.token.size() == 64;
}
#endif

void forget() {
    std::error_code ec;
    fs::remove(userFolder() / "session", ec);
}

Settings loadSettings() {
    Settings settings;
    const core::Json said = parse(readFile(userFolder() / "launcher.json"), "launcher.json");
    const std::string install = said["install"].stringOr("");
    settings.install = install.empty() ? defaultInstall() : pathOf(install);
    settings.email = said["email"].stringOr("");
    return settings;
}

void saveSettings(const Settings& settings) {
    writeFile(userFolder() / "launcher.json",
              "{\"install\": " + quoted(utf8Of(settings.install)) + ", \"email\": " + quoted(settings.email) + "}\n");
}

// The first run's graphics, in the keys the game's readOptions knows (core/args.cpp): the preset
// this machine can draw -- Low, Medium or High -- and nothing else, so the run's own switches for
// the display, the frame cap and the volume stay as they are. The file is the player's from the
// moment it exists: the game writes it back whole whenever he changes a row in Options, and a
// launcher that overwrote it on every start would lose his choices.
bool firstRunGraphics(const Gpu& gpu) {
    if (gpu.preset < 0) return false;
    const fs::path options = userFolder() / "options.txt";
    std::error_code error;
    if (fs::exists(options, error)) return false;
    const core::GraphicsPreset& set = core::kGraphicsPresets[gpu.preset];
    const std::string keys = "scale " + std::to_string(set.scale) + "\nmsaa " + std::to_string(set.msaa) +
                             "\nshadows " + std::to_string(set.shadows) + "\nssao " + (set.ssao ? "1" : "0") +
                             "\nbloom " + (set.bloom ? "1" : "0") + "\nreflections " +
                             (set.reflections ? "1" : "0") + "\ngrass " + std::to_string(set.grass) + "\n";
    if (!writeFile(options, keys)) return false;
    core::logf("launcher: graphics: first run at %s for %s", set.name, gpu.name.c_str());
    return true;
}

// The machine, told to the site once a run (web/server.py /api/machine): which card, which
// driver behind it, what runs around it -- the fleet an update has to fit, read off
// machines.jsonl on the box. Where no probe ran (the Mac until L5) no report goes: an empty card
// is not fleet data, and nothing an account could be found by is ever in the body.
bool reportMachine(const Place& place, const Gpu& gpu, bool tuned) {
    if (!gpu.known) return false;
    const std::string cpu = cpuName(), os = osVersion();
    const int ram = ramMb();
    char vendor[8];
    std::snprintf(vendor, sizeof vendor, "%04x", gpu.vendorId);
    const std::string body =
        "{\"gpu\":" + quoted(gpu.name) + ",\"vendor\":\"" + vendor + "\",\"driver\":" + quoted(gpu.driver) +
        ",\"date\":" + quoted(gpu.date) + ",\"vulkan\":" + (gpu.vulkan ? "1" : "0") +
        ",\"old\":" + (gpu.driverOld ? "1" : "0") + ",\"software\":" + (gpu.software ? "1" : "0") +
        ",\"memory\":" + std::to_string(gpu.dedicated >> 20) + ",\"graphics\":" +
        quoted(gpu.preset >= 0 ? core::kGraphicsPresets[gpu.preset].name : "") + ",\"tuned\":" +
        (tuned ? "1" : "0") + ",\"cpu\":" + quoted(cpu) + ",\"ram\":" + std::to_string(ram) +
        ",\"os\":" + quoted(os) + "}";
    const Reply reply = request("POST", place.site + "/api/machine", body, {kJson});
    if (reply.status != 200) {
        core::logError("launcher: machine report: %s",
                       reply.status ? ("HTTP " + std::to_string(reply.status)).c_str() : reply.error.c_str());
        return false;
    }
    core::logf("launcher: machine report sent");
    return true;
}

// ---- the release --------------------------------------------------------------------------------

bool fetchManifest(const Place& place, Manifest& out, std::string& error) {
    const Reply reply = request("GET", place.site + "/game/" + place.platform + "/manifest.json");
    if (reply.status != 200) {
        error = reply.status == 0 ? reply.error : "status " + std::to_string(reply.status);
        return false;
    }
    const core::Json said = parse(reply.body, "the release's manifest");
    out.version = said["version"].stringOr("");
    out.protocol = int(said["protocol"].numberOr(0));
    out.exe = said["exe"].stringOr("MU2.exe");
    out.bytes = uint64_t(said["bytes"].numberOr(0));
    // HTTPS only, and a folder: the hash is added to it.
    out.filesUrl = said["files_url"].stringOr("");
    if (out.filesUrl.rfind("https://", 0) != 0) out.filesUrl.clear();
    if (!out.filesUrl.empty() && out.filesUrl.back() != '/') out.filesUrl += '/';
    out.files.clear();
    const core::Json& files = said["files"];
    for (size_t i = 0; i < files.size(); ++i) {
        const core::Json& f = files.at(i);
        Entry e{f["path"].stringOr(""), uint64_t(f["size"].numberOr(0)), f["sha256"].stringOr("")};
        // Nothing outside the install: no absolute path, no "..".
        if (e.path.empty() || e.path[0] == '/' || e.path.find("..") != std::string::npos || e.sha256.size() != 64) {
            error = "a file of no shape in the manifest";
            return false;
        }
        out.files.push_back(std::move(e));
    }
    if (out.version.empty() || out.files.empty()) {
        error = "an empty manifest";
        return false;
    }
    return true;
}

namespace {

struct Record {
    uint64_t size = 0;
    std::string stamp;
    std::string sha256;
};

std::map<std::string, Record> records(const fs::path& install, std::string* version = nullptr) {
    std::map<std::string, Record> out;
    const core::Json said = parse(readFile(install / "installed.json"), "installed.json");
    if (version) *version = said["version"].stringOr("");
    const core::Json& files = said["files"];
    for (const auto& [path, f] : files.members) {
        out[path] = Record{uint64_t(f["size"].numberOr(0)), f["stamp"].stringOr(""), f["sha256"].stringOr("")};
    }
    return out;
}

void record(const fs::path& install, const Manifest& manifest) {
    std::string json = "{\"version\": " + quoted(manifest.version) + ", \"files\": {";
    bool first = true;
    for (const Entry& e : manifest.files) {
        const fs::path at = install / pathOf(e.path);
        json += (first ? "\n " : ",\n ") + quoted(e.path) + ": {\"size\": " + std::to_string(e.size) +
                ", \"stamp\": " + quoted(stamp(at)) + ", \"sha256\": " + quoted(e.sha256) + "}";
        first = false;
    }
    writeFile(install / "installed.json", json + "\n}}\n");
}

}  // namespace

std::string installedVersion(const fs::path& install) {
    std::string version;
    records(install, &version);
    return version;
}

std::vector<Entry> stale(const fs::path& install, const Manifest& manifest, Progress& progress) {
    const auto known = records(install);
    std::vector<Entry> out;
    progress.done = 0;
    progress.total = manifest.bytes;
    progress.files = int(manifest.files.size());
    for (size_t i = 0; i < manifest.files.size(); ++i) {
        const Entry& e = manifest.files[i];
        progress.file = int(i);
        const fs::path at = install / pathOf(e.path);
        std::error_code ec;
        const uint64_t size = fs::exists(at, ec) ? fs::file_size(at, ec) : UINT64_MAX;
        if (size != e.size) {
            out.push_back(e);
            progress.done += e.size;
            continue;
        }
        const auto it = known.find(e.path);
        if (it != known.end() && it->second.size == size && it->second.stamp == stamp(at)) {
            if (it->second.sha256 != e.sha256) out.push_back(e);
            progress.done += e.size;
            continue;
        }
        if (sha256File(at, [&](uint64_t n) { progress.done += n; }) != e.sha256) out.push_back(e);
    }
    return out;
}

bool update(const Place& place, const fs::path& install, const Manifest& manifest, const std::vector<Entry>& todo,
            Progress& progress, const std::atomic<bool>& stop, std::string& error) {
    uint64_t total = 0;
    for (const Entry& e : todo) total += e.size;
    progress.done = 0;
    progress.total = total;
    progress.files = int(todo.size());
    for (size_t i = 0; i < todo.size(); ++i) {
        const Entry& e = todo[i];
        progress.file = int(i);
        const fs::path at = install / pathOf(e.path);
        const fs::path part = at.string() + ".part";
        std::error_code ec;
        fs::create_directories(at.parent_path(), ec);
        const uint64_t before = progress.done;
        // From where the manifest keeps its files, twice; then once more from the box, which
        // holds them until a release has moved off it. Every copy is checked by its hash.
        const std::string box = place.site + "/game/" + place.platform + "/files/" + e.sha256;
        const bool elsewhere = !manifest.filesUrl.empty();
        const std::string primary = elsewhere ? manifest.filesUrl + e.sha256 : box;
        bool whole = false;
        // Twice at most: a part left by an earlier run that turns out wrong is fetched again clean.
        for (int attempt = 0; attempt < (elsewhere ? 3 : 2) && !whole; ++attempt) {
            const std::string& url = attempt < 2 ? primary : box;
            const uint64_t had = fs::exists(part, ec) ? fs::file_size(part, ec) : 0;
            progress.done = before + (attempt == 0 ? std::min(had, e.size) : 0);
            if (!download(url, part, attempt == 0 && had < e.size, [&](uint64_t n) { progress.done += n; }, stop, error)) {
                if (stop.load()) return false;
                core::logError("launcher: %s: %s", e.path.c_str(), error.c_str());
                fs::remove(part, ec);
                continue;
            }
            if (sha256File(part) == e.sha256) {
                whole = true;
            } else {
                core::logError("launcher: %s came down wrong; fetched again", e.path.c_str());
                fs::remove(part, ec);
            }
        }
        if (!whole) {
            error = "Could not download " + e.path + ". Check your connection and try again.";
            return false;
        }
        fs::remove(at, ec);
        fs::rename(part, at, ec);
        if (ec) {
            error = "Could not replace " + e.path + ". Close the game and try again.";
            return false;
        }
        progress.done = before + e.size;
    }
    record(install, manifest);
    return true;
}

std::vector<fs::path> crashFolders(const fs::path& install) {
    return {install / "logs", userFolder() / "logs"};
}

bool play(const fs::path& install, const Manifest& manifest, const GameTicket& ticket, std::string& error) {
    const fs::path exe = install / pathOf(manifest.exe);
    const std::string handed = ticket.ticket + ":" + ticket.secret;
#ifdef _WIN32
    // Inherited from ours, set only around the start so it never lingers here.
    SetEnvironmentVariableW(L"MU2_TICKET", std::wstring(handed.begin(), handed.end()).c_str());
    std::wstring line = L"\"" + exe.wstring() + L"\"";
    STARTUPINFOW start{};
    start.cb = sizeof start;
    PROCESS_INFORMATION running{};
    const BOOL ok = CreateProcessW(exe.wstring().c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                   install.wstring().c_str(), &start, &running);
    SetEnvironmentVariableW(L"MU2_TICKET", nullptr);
    if (!ok) {
        error = "Could not start the game (" + std::to_string(GetLastError()) + ").";
        return false;
    }
    CloseHandle(running.hThread);
    CloseHandle(running.hProcess);
    return true;
#else
    std::vector<std::string> env;
    for (char** e = environ; *e; ++e) {
        if (std::string(*e).rfind("MU2_TICKET=", 0) != 0 && std::string(*e).rfind("MU2_SESSION=", 0) != 0) {
            env.emplace_back(*e);
        }
    }
    env.push_back("MU2_TICKET=" + handed);
    std::vector<char*> envp;
    for (std::string& s : env) envp.push_back(s.data());
    envp.push_back(nullptr);
    // The fetches carry content, not modes: the game's own binary lands 644 from curl, and a
    // spawn of it fails until it is executable. Set every run, before the first -- a permission
    // the file was given once is not a permission the launcher can trust to still be there.
    std::error_code ec;
    fs::permissions(exe, fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read, ec);
    std::string path = exe.string();
    char* argv[] = {path.data(), nullptr};
    pid_t pid = 0;
    if (posix_spawn(&pid, path.c_str(), nullptr, nullptr, argv, envp.data()) != 0) {
        error = "Could not start the game.";
        return false;
    }
    return true;
#endif
}

}  // namespace mu::launcher
