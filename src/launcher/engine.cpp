#include "launcher/engine.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <thread>

#include "core/args.h"
#include "core/json.h"
#include "core/log.h"
#include "launcher/gpu.h"
#include "launcher/https.h"
#include "launcher/self_update.h"
#include "launcher/sha256.h"
#include "net/socket.h"
#include "net/wire.h"

#ifdef _WIN32
#include <windows.h>
#include <dpapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#else
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#ifdef __linux__
#include <sys/sysinfo.h>
#else
#include <sys/sysctl.h>
#endif
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
    // Over the old one in one step (MoveFileEx's replace on Windows): removing it first lost the
    // sign-in, the settings or installed.json whenever the rename then failed -- an antivirus
    // holding the new file a moment, on a player's PC.
    for (int attempt = 0; attempt < 5; ++attempt) {
        ec.clear();
        fs::rename(part, path, ec);
        if (!ec) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return false;
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
    // Through void(*)(), GCC's own way to say a FARPROC is cast on purpose.
    const auto rtl = ntdll ? reinterpret_cast<RtlGetVersion>(reinterpret_cast<void (*)()>(
                                 GetProcAddress(ntdll, "RtlGetVersion")))
                           : nullptr;
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof version;
    if (rtl == nullptr || rtl(&version) != 0) return {};
    return std::to_string(version.dwMajorVersion) + "." + std::to_string(version.dwMinorVersion) + "." +
           std::to_string(version.dwBuildNumber);
}
#elif defined(__linux__)
// The same three for Linux (docs/plans/linux-release.md, X5): the CPU's name from /proc/cpuinfo,
// the machine's memory, and the distro as it names itself ("Ubuntu 24.04.1 LTS").
std::string fieldOf(const fs::path& file, const std::string& key, char split) {
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind(key, 0) != 0) continue;
        const size_t at = line.find(split);
        if (at == std::string::npos) continue;
        std::string value = line.substr(at + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '"')) value.erase(0, 1);
        while (!value.empty() && (value.back() == '"' || value.back() == ' ')) value.pop_back();
        return value;
    }
    return {};
}

std::string cpuName() { return fieldOf("/proc/cpuinfo", "model name", ':'); }

int ramMb() {
    struct sysinfo info {};
    return sysinfo(&info) == 0 ? int((uint64_t(info.totalram) * info.mem_unit) >> 20) : 0;
}

std::string osVersion() { return fieldOf("/etc/os-release", "PRETTY_NAME", '='); }
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
#elif defined(__linux__)
// $XDG_DATA_HOME/MU2, ~/.local/share/MU2 without it: the game's own user folder (core/paths.cpp),
// so the first run's options.txt is the one the game reads.
fs::path userFolder() {
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg == '/') return fs::path(xdg) / "MU2";
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : ".") / ".local" / "share" / "MU2";
}
// Beside the player's files, in a folder of its own that an uninstall may remove whole.
fs::path defaultInstall() { return userFolder() / "MU Classic"; }
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
#endif

#ifndef _WIN32
// The Mac and Linux keep the sign-in in a file only its owner may read.
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

const std::vector<Region>& regions() {
    // The Hetzner box (net::kDefaultHost), the game's own default.
    static const std::vector<Region> all = {
        {"eu-north", "North Europe", "Helsinki", "mu-classic.com"},
        // Vultr São Paulo (server/README.md, the South America box).
        {"sa-east", "South America", "Sao Paulo", "216.238.127.33"},  // no ã in the launcher's face
        // Hetzner Singapore (server/README.md, the Southeast Asia box).
        {"ap-southeast", "Southeast Asia", "Singapore", "5.223.83.101"},
    };
    return all;
}

const Region& regionNamed(const std::string& id) {
    for (const Region& one : regions()) {
        if (one.id == id) return one;
    }
    return regions().front();
}

double pingMs(const Region& region, double seconds) {
    const size_t colon = region.server.rfind(':');
    const std::string host = colon == std::string::npos ? region.server : region.server.substr(0, colon);
    const int port = colon == std::string::npos ? net::kDefaultPort : std::atoi(region.server.c_str() + colon + 1);
    double best = -1.0;
    for (int attempt = 0; attempt < 2; ++attempt) {
        net::Socket socket;
        std::string error;
        const auto from = std::chrono::steady_clock::now();
        if (!socket.connect(host, port, seconds, error)) continue;
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - from).count();
        if (best < 0.0 || ms < best) best = ms;
    }
    return best;
}

std::string quickest(const std::vector<double>& pings) {
    std::string id;
    double best = -1.0;
    for (size_t i = 0; i < pings.size() && i < regions().size(); ++i) {
        if (pings[i] >= 0.0 && (best < 0.0 || pings[i] < best)) {
            best = pings[i];
            id = regions()[i].id;
        }
    }
    return id;
}

Settings loadSettings() {
    Settings settings;
    const core::Json said = parse(readFile(userFolder() / "launcher.json"), "launcher.json");
    const std::string install = said["install"].stringOr("");
    settings.install = install.empty() ? defaultInstall() : pathOf(install);
    settings.email = said["email"].stringOr("");
    settings.region = regionNamed(said["region"].stringOr("")).id;
    // A pick of a region since taken away is no pick.
    settings.regionPicked = said["regionPicked"].boolOr(false) && settings.region == said["region"].stringOr("");
    return settings;
}

void saveSettings(const Settings& settings) {
    writeFile(userFolder() / "launcher.json", "{\"install\": " + quoted(utf8Of(settings.install)) +
                                                  ", \"email\": " + quoted(settings.email) +
                                                  ", \"region\": " + quoted(settings.region) +
                                                  ", \"regionPicked\": " + (settings.regionPicked ? "true" : "false") + "}\n");
}

// The first run's graphics, in the keys the game's readOptions knows (core/args.cpp): the preset
// this machine can draw -- Medium or High, or the low-end set with its 60 fps cap where it would be
// Lowest or Low (firstRunSet) -- and nothing else, so the run's own switches for the display and
// the volume stay as they are. The file is the player's from the
// moment it exists: the game writes it back whole whenever he changes a row in Options, and a
// launcher that overwrote it on every start would lose his choices.
bool firstRunGraphics(const Gpu& gpu) {
    if (gpu.preset < 0) return false;
    const fs::path options = userFolder() / "options.txt";
    std::error_code error;
    if (fs::exists(options, error)) return false;
    const core::GraphicsPreset& set = firstRunSet(gpu);
    const bool lowEnd = &set == &core::kLowEndFirstRun;
    const std::string keys ="scale " + std::to_string(set.scale) + "\nmsaa " + std::to_string(set.msaa) +
                             "\nshadows " + std::to_string(set.shadows) + "\nssao " + (set.ssao ? "1" : "0") +
                             "\nbloom " + (set.bloom ? "1" : "0") + "\nreflections " +
                             (set.reflections ? "1" : "0") + "\ngrass " + std::to_string(set.grass) +
                             "\nlights " + (set.lights ? "1" : "0") + "\ntextures " +
                             std::to_string(set.textures) + "\nrenderer " + std::to_string(gpu.renderer) + "\n" +
                             (lowEnd ? "cap " + std::to_string(core::kLowEndCap) + "\n" : "");
    if (!writeFile(options, keys)) return false;
    core::logf("launcher: graphics: first run at %s on %s for %s", set.name,
               gpu.renderer == 2 ? "Direct3D 11" : "Vulkan", gpu.name.c_str());
    return true;
}

// The machine, told to the site once a run (web/server.py /api/machine): which card, which
// driver behind it, what runs around it -- the fleet an update has to fit, read off
// machines.jsonl on the box. Where no probe ran (the Mac until L5) no report goes: an empty card
// is not fleet data, and nothing an account could be found by is ever in the body. A signed-in
// launcher sends its token beside it, so the account page lists the machine (web/server.py);
// the fleet's file still holds no account.
bool reportMachine(const Place& place, const Gpu& gpu, bool tuned, const std::string& token) {
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
        quoted(gpu.preset >= 0 ? core::kGraphicsPresets[gpu.preset].name : "") +
        ",\"renderer\":" + std::to_string(gpu.renderer) + ",\"tuned\":" +
        (tuned ? "1" : "0") + ",\"cpu\":" + quoted(cpu) + ",\"ram\":" + std::to_string(ram) +
        ",\"os\":" + quoted(os) + "}";
    std::vector<std::string> headers = {kJson};
    if (!token.empty()) headers.push_back("Authorization: Bearer " + token);
    const Reply reply = request("POST", place.site + "/api/machine", body, headers);
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
        // Nothing outside the install: no absolute path, no "..", and on Windows no drive ("C:")
        // and no backslash, either of which makes `install / path` a path of its own.
        if (e.path.empty() || e.path[0] == '/' || e.path.find("..") != std::string::npos ||
            e.path.find_first_of(":\\") != std::string::npos || e.sha256.size() != 64) {
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
    // A folder the player may not write -- Program Files, a read-only drive -- said as that, not
    // as the download that fails after it.
    {
        std::error_code ec;
        fs::create_directories(install, ec);
        const fs::path probe = install / ".mu2-write-test";
        std::ofstream test(probe, std::ios::binary | std::ios::trunc);
        const bool writable = bool(test);
        test.close();
        fs::remove(probe, ec);
        if (!writable) {
            error = "Can't write to " + utf8Of(install) + ". Choose another folder.";
            return false;
        }
    }
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
        // Over the old file in one step, a few times: an antivirus scanning the fresh part, or
        // the old exe a moment after the game closed, refuses it for a second or so.
        for (int attempt = 0; attempt < 10; ++attempt) {
            ec.clear();
            fs::rename(part, at, ec);
            if (!ec) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
        if (ec) {
            error = "Could not replace " + e.path + ". Close the game and try again.";
            return false;
        }
        progress.done = before + e.size;
    }
    record(install, manifest);
    return true;
}

void refreshShortcut() {
#ifdef _WIN32
    // Launchers before 4eed2ef4 had no icon, and Explorer keeps what it drew for a shortcut
    // after the exe under it is replaced: told the shortcut changed, it reads the icon again.
    const fs::path desktop = known(FOLDERID_Desktop);
    if (desktop.empty()) return;
    const fs::path shortcut = desktop / "MU Classic.lnk";
    std::error_code ec;
    if (fs::exists(shortcut, ec)) SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, shortcut.c_str(), nullptr);
#endif
}

bool uninstall(const fs::path& install, const Manifest& manifest, std::string& error) {
    std::set<std::string> paths;
    for (const auto& [path, recorded] : records(install)) paths.insert(path);
    for (const Entry& e : manifest.files) paths.insert(e.path);
    std::set<fs::path> folders;
    int removed = 0, stuck = 0;
    for (const std::string& path : paths) {
        const fs::path relative = pathOf(path);
        // Only what lies inside the install: a list that names anything else is not obeyed.
        if (relative.empty() || relative.is_absolute() ||
            std::any_of(relative.begin(), relative.end(), [](const fs::path& part) { return part == ".."; })) {
            continue;
        }
        const fs::path at = install / relative;
        fs::path part = at;
        part += ".part";
        for (const fs::path& file : {at, part}) {
            std::error_code ec;
            if (!fs::exists(file, ec)) continue;
            if (fs::remove(file, ec)) {
                ++removed;
            } else {
                ++stuck;
                core::logError("launcher: uninstall: %s stays (%s)", utf8Of(file).c_str(), ec.message().c_str());
            }
        }
        for (fs::path d = at.parent_path(); d != install && !d.empty() && d != d.parent_path(); d = d.parent_path()) {
            folders.insert(d);
        }
    }
    if (stuck) {
        error = "Couldn't remove " + std::to_string(stuck) + (stuck == 1 ? " file" : " files") +
                ". Close the game and try again.";
        return false;
    }
    std::error_code ec;
    fs::remove(install / "installed.json", ec);
    // The deepest first; a folder with anything else in it (logs, the launcher) stays.
    std::vector<fs::path> deepest(folders.begin(), folders.end());
    std::sort(deepest.begin(), deepest.end(),
              [](const fs::path& a, const fs::path& b) { return a.native().size() > b.native().size(); });
    for (const fs::path& d : deepest) fs::remove(d, ec);
    // The install itself only when it is the game's own folder, never a ~/Applications.
    if (install.filename() == "MU2" || install.filename() == "MU Classic") fs::remove(install, ec);
    fs::remove(userFolder() / "options.txt", ec);
    core::logf("launcher: uninstalled from %s, %d files", utf8Of(install).c_str(), removed);
    return true;
}

std::vector<fs::path> crashFolders(const fs::path& install) {
    return {install / "logs", userFolder() / "logs"};
}

bool play(const fs::path& install, const Manifest& manifest, const GameTicket& ticket, const std::string& server,
          std::string& error, unsigned long* pid) {
    const fs::path exe = install / pathOf(manifest.exe);
    const std::string handed = ticket.ticket + ":" + ticket.secret;
#ifdef _WIN32
    // Inherited from ours, set only around the start so it never lingers here.
    SetEnvironmentVariableW(L"MU2_TICKET", std::wstring(handed.begin(), handed.end()).c_str());
    std::wstring line = L"\"" + exe.wstring() + L"\"";
    if (!server.empty()) line += L" --server " + std::wstring(server.begin(), server.end());
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
    // The game may take the front from us (gfx::Window::toFront): Windows lets a window come
    // forward only from the program the player last used, and the launcher minimises itself
    // next, before the game's window is up -- it opened behind (the user, 2026-10-15).
    AllowSetForegroundWindow(running.dwProcessId);
    if (pid) *pid = running.dwProcessId;
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
    std::string flag = "--server", where = server;
    std::vector<char*> argv = {path.data()};
    if (!server.empty()) {
        argv.push_back(flag.data());
        argv.push_back(where.data());
    }
    argv.push_back(nullptr);
    pid_t child = 0;
    if (posix_spawn(&child, path.c_str(), nullptr, nullptr, argv.data(), envp.data()) != 0) {
        error = "Could not start the game.";
        return false;
    }
    if (pid) *pid = static_cast<unsigned long>(child);
    return true;
#endif
}

void handFront(unsigned long pid) {
#ifdef _WIN32
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) return;
    struct Search {
        DWORD pid;
        HWND found;
    } search{pid, nullptr};
    // The game's own window: visible, top-level, owned by nothing (not a console or a box).
    const auto look = [](HWND w, LPARAM data) -> BOOL {
        auto* s = reinterpret_cast<Search*>(data);
        DWORD owner = 0;
        GetWindowThreadProcessId(w, &owner);
        if (owner == s->pid && IsWindowVisible(w) && !GetWindow(w, GW_OWNER)) {
            s->found = w;
            return FALSE;
        }
        return TRUE;
    };
    for (int i = 0; i < 600 && !search.found; ++i) {
        if (WaitForSingleObject(process, 100) != WAIT_TIMEOUT) break;  // gone, or asked wrong
        EnumWindows(look, reinterpret_cast<LPARAM>(&search));
    }
    CloseHandle(process);
    if (!search.found) {
        core::logf("play: the game's window never showed; the launcher minimises anyway");
        return;
    }
    if (IsIconic(search.found)) ShowWindow(search.found, SW_RESTORE);
    const bool ok = SetForegroundWindow(search.found);
    core::logf("play: the game's window %s", ok ? "handed the front" : "refused the front");
#else
    (void)pid;
#endif
}

bool gameRunning(unsigned long pid) {
    if (pid == 0) return false;
#ifdef _WIN32
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!process) return false;
    const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return alive;
#else
    int status = 0;
    return waitpid(static_cast<pid_t>(pid), &status, WNOHANG) == 0;
#endif
}

}  // namespace mu::launcher
