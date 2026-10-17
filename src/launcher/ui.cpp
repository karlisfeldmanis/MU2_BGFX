// MU2Launcher's screens. One window of 960×540 design units, no system frame: the wallpaper, the
// MU Classic logo, and the game's own slab buttons (src/game/ui/controls.cpp's looks, copied
// here so the launcher links none of the game). Drawn with the game's canvas (gfx::Interface);
// its shaders, fonts and pictures are inside the exe (embedded.h), handed to core::readFile.
//
// Everything that waits -- the website, the hashing, the downloads -- runs on one worker thread
// (Work); the screen reads its state each frame and never waits.
#include "launcher/ui.h"

#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#elif defined(__linux__)
// X11, as the game (gfx/window.cpp): every Wayland desktop runs it through XWayland.
#define GLFW_EXPOSE_NATIVE_X11
#include <spawn.h>
#include <sys/wait.h>

#include <cstdio>
#include <initializer_list>
extern char** environ;
#else
#define GLFW_EXPOSE_NATIVE_COCOA
#include <spawn.h>
extern char** environ;
#endif
#include <GLFW/glfw3native.h>

#include <bgfx/bgfx.h>
#include <bimg/decode.h>
#include <bx/allocator.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <mutex>
#include <thread>

#include "core/files.h"
#include "core/log.h"
#include "gfx/callback.h"
#include "gfx/face.h"
#include "gfx/interface.h"
#include "gfx/window.h"  // gfx::shaderProfile
#include "launcher/crash_upload.h"
#include "launcher/driver.h"
#include "launcher/embedded.h"
#include "launcher/gpu.h"
#include "launcher/self_update.h"
#include "net/socket.h"

namespace mu::launcher {

namespace {

using gfx::Box;
using Clock = std::chrono::steady_clock;

constexpr float kW = 960.0f, kH = 540.0f;  // the design's units

// ---- files inside the exe -----------------------------------------------------------------------

// By the end of the path: a font or a picture by its leaf, a shader by its profile folder and
// leaf ("embedded/dxbc/vs_overlay.sc.bin" is "dxbc/vs_overlay.sc.bin").
const Embedded* find(const std::string& path) {
    for (const Embedded& e : embedded()) {
        const size_t n = std::char_traits<char>::length(e.name);
        if (path.size() >= n && path.compare(path.size() - n, n, e.name) == 0 &&
            (path.size() == n || path[path.size() - n - 1] == '/' || path[path.size() - n - 1] == '\\'))
            return &e;
    }
    return nullptr;
}

bool readEmbedded(const std::string& path, std::vector<uint8_t>& out) {
    const Embedded* e = find(path);
    if (!e) return false;
    out.assign(e->bytes, e->bytes + e->size);
    return true;
}
bool hasEmbedded(const std::string& path) { return find(path) != nullptr; }
std::vector<std::string> listEmbedded(const std::string&) { return {}; }
const core::FileSource kInside{readEmbedded, hasEmbedded, listEmbedded};

// ---- colour, and the game's looks ---------------------------------------------------------------

uint32_t hex(uint32_t rgb, float a = 1.0f) {
    return gfx::rgba(float((rgb >> 16) & 255) / 255.0f, float((rgb >> 8) & 255) / 255.0f, float(rgb & 255) / 255.0f, a);
}
uint32_t tone(const float* c, float alpha = 1.0f) { return gfx::rgba(c[0], c[1], c[2], alpha); }

// controls.cpp's Look: the rim top to foot, the body top to foot, the word, the glow round it, the
// light along its top, the ember rising from its foot.
struct Look {
    float rimTop[3], rimFoot[3], top[3], foot[3], ink[3];
    float glow, highlight, ember;
};
constexpr Look kPrimaryRest{{0.831f, 0.286f, 0.231f}, {0.369f, 0.082f, 0.055f}, {0.478f, 0.102f, 0.075f},
                            {0.227f, 0.043f, 0.027f}, {0.965f, 0.906f, 0.816f}, 0.25f, 0.25f, 0.0f};
constexpr Look kPrimaryOver{{0.941f, 0.380f, 0.314f}, {0.478f, 0.110f, 0.075f}, {0.580f, 0.137f, 0.102f},
                            {0.290f, 0.059f, 0.039f}, {1.0f, 1.0f, 1.0f}, 0.55f, 0.30f, 0.0f};
constexpr Look kPrimaryHeld{{0.369f, 0.082f, 0.055f}, {0.831f, 0.286f, 0.231f}, {0.165f, 0.031f, 0.020f},
                            {0.353f, 0.071f, 0.047f}, {0.965f, 0.906f, 0.816f}, 0.0f, 0.0f, 0.0f};
constexpr Look kSecondaryRest{{0.420f, 0.337f, 0.271f}, {0.169f, 0.133f, 0.106f}, {0.141f, 0.106f, 0.086f},
                              {0.067f, 0.047f, 0.039f}, {0.788f, 0.749f, 0.682f}, 0.0f, 0.06f, 0.0f};
constexpr Look kSecondaryOver{{0.659f, 0.545f, 0.424f}, {0.290f, 0.227f, 0.180f}, {0.180f, 0.133f, 0.110f},
                              {0.082f, 0.063f, 0.051f}, {1.0f, 0.957f, 0.886f}, 0.18f, 0.06f, 0.30f};
constexpr Look kOff{{0.180f, 0.145f, 0.118f}, {0.102f, 0.078f, 0.067f}, {0.094f, 0.071f, 0.059f},
                    {0.051f, 0.035f, 0.031f}, {0.361f, 0.322f, 0.286f}, 0.0f, 0.0f, 0.0f};
constexpr float kBlood[3] = {0.761f, 0.212f, 0.169f};

constexpr uint32_t kBoneHi = 0xF3E8D4, kBone = 0xD9CFBD, kLabel = 0xC9BFAE, kQuiet = 0x8F8575;
constexpr uint32_t kAmber = 0xE0A458;  // a word of caution, not an error: the driver beside the card

// ---- the window's state -------------------------------------------------------------------------

enum class Screen { SignIn, Home };
enum class Home { Checking, Install, Update, Downloading, Paused, Ready, Offline, Failed, Uninstall, Removing };

struct Field {
    std::string text;
    bool secret = false;
};

class Launcher {
public:
    explicit Launcher(const UiOptions& options) : options_(options), place_(options.place) {}
    int run();

private:
    // window and frame
    bool open();
    void close();
    void frame();
    void draw();
    void drawSignIn();
    void drawHome();
    void drawChrome();

    // drawing
    void panel(const Box& b, float radius, uint32_t top, uint32_t foot);
    bool button(const Box& b, const std::string& word, bool primary, bool off = false, float size = 18.0f);
    bool flat(float right, float y, const std::string& word, float* width = nullptr);
    bool link(float x, float baseline, const std::string& word, uint32_t ink, float size = 15.0f);
    void words(float x, float baseline, const std::string& s, uint32_t ink, float size, bool bold = false);
    float measure(const std::string& s, float size, bool bold = false) const;
    void field(const Box& b, Field& f, int index, const char* placeholder);
    void alert(const Box& b, const std::string& message);
    void drawRegion(float x, float baseline);
    void drawRegionList();
    Box px(const Box& b) const { return {b.x * s_, b.y * s_, b.w * s_, b.h * s_}; }
    bool over(const Box& b) const { return b.has(mx_, my_); }

    // what the player asks
    void signIn();
    void check();
    void install();
    void uninstallNow();
    void playNow();
    void start();
    void sendCrashesAfter(int delay, const char* launcherLog = nullptr);
    void logOut();
    void openUrl(const std::string& url);
    void pickFolder();
    void measureRegions();

    // the worker
    void work(std::function<void()> job);
    void post(std::function<void()> then);

    const UiOptions options_;
    Place place_;
    Settings settings_;
    Gpu gpu_;                // what this machine draws with, probed once at start
    bool tuned_ = false;     // firstRunGraphics wrote options.txt this run
    bool reported_ = false;  // this run's machine report is away (engine.cpp reportMachine)
    Signed who_;
    Manifest manifest_;
    // When the release was last asked about, and what follows from it: an update found is
    // fetched as soon as the worker is free (autoInstall_), and a Play that asked first starts
    // the game once the install is current (playAfter_). Players leave the launcher open, so it
    // asks again on Play and every kRecheck (frame), not only when it opens.
    Clock::time_point checkedAt_{};
    bool autoInstall_ = false;
    bool playAfter_ = false;
    // The game's check waits for the launcher's own update (run): set by its answer, taken by
    // frame() once the worker is free.
    bool checkPending_ = false;
    std::vector<Entry> todo_;
    uint64_t todoBytes_ = 0;

    Screen screen_ = Screen::SignIn;
    Home home_ = Home::Checking;
    std::string message_;  // the sign-in's refusal, or why the home screen failed
    bool busy_ = false;    // the sign-in's worker is out
    bool starting_ = false;  // Play's ticket is being asked for (start)
    unsigned long game_ = 0;  // the game play() started, while it runs (gameRunning); 0 when none
    bool bgfxUp_ = false;    // bgfx started (open): close() shuts down only what started
    bool stay_ = true;
    // The region's list is open (drawRegion), and the left edge of the top strip's words: a press
    // left of it on the strip moves the window.
    bool regionsOpen_ = false;
    Box regionAt_{};
    float dragRight_ = kW - 260.0f;
    // Each region's round trip in ms (engine.h pingMs), in regions()' order; empty until measured,
    // -1 for one that did not answer. Measured on pinger_ once a run.
    std::vector<double> pings_;
    std::thread pinger_;

    Field fields_[2];  // email, password
    int focus_ = 0;
    std::string typed_;
    int backspaces_ = 0;
    bool enter_ = false, tab_ = false, paste_ = false;

    GLFWwindow* window_ = nullptr;
    GLFWcursor* hand_ = nullptr;
    GLFWcursor* beam_ = nullptr;
    GLFWcursor* wantCursor_ = nullptr;
    gfx::Callback callback_;
    bgfx::SwapChain chain_;
    gfx::Interface iface_;
    gfx::Canvas canvas_;
    gfx::Face word_, body_;
    bgfx::TextureHandle wordTex_ = BGFX_INVALID_HANDLE, bodyTex_ = BGFX_INVALID_HANDLE;
    gfx::Art wallpaper_, logo_;
    int fbW_ = 0, fbH_ = 0;
    float s_ = 1.0f;  // framebuffer pixels per design unit
    float mx_ = -1, my_ = -1;
    bool down_ = false, clicked_ = false, released_ = false;
    bool dragging_ = false;
    double dragX_ = 0, dragY_ = 0;
    int pressedAt_ = -1;  // which button the press began on, so a drag off it is no click
    int widgetIndex_ = 0;
    Clock::time_point started_ = Clock::now();

    std::thread worker_;
    std::atomic<bool> working_{false};
    std::atomic<bool> stop_{false};
    std::mutex posted_;
    std::vector<std::function<void()>> then_;
    Progress progress_;
    // the download's speed, sampled about once a second
    uint64_t sampledBytes_ = 0;
    Clock::time_point sampledAt_ = Clock::now();
    double speed_ = 0.0;
};

// ---- the worker ---------------------------------------------------------------------------------

void Launcher::work(std::function<void()> job) {
    if (working_.load()) return;
    if (worker_.joinable()) worker_.join();
    working_ = true;
    stop_ = false;
    worker_ = std::thread([this, job] {
        job();
        working_ = false;
        glfwPostEmptyEvent();
    });
}

void Launcher::post(std::function<void()> then) {
    std::lock_guard<std::mutex> hold(posted_);
    then_.push_back(std::move(then));
}

// ---- what the player asks -----------------------------------------------------------------------

void Launcher::signIn() {
    const std::string email = fields_[0].text, password = fields_[1].text;
    if (email.empty() || password.empty()) {
        message_ = email.empty() ? "Enter your email." : "Enter your password.";
        focus_ = email.empty() ? 0 : 1;
        return;
    }
    busy_ = true;
    message_.clear();
    const bool stay = stay_;
    work([this, email, password, stay] {
        Signed who;
        std::string message;
        const Answer a = launcher::signIn(place_, email, password, who, message);
        post([this, a, who, message, stay, email] {
            busy_ = false;
            if (a != Answer::Ok) {
                message_ = message;
                fields_[1].text.clear();
                focus_ = 1;
                return;
            }
            who_ = who;
            if (stay) keep(who_);
            settings_.email = email;
            saveSettings(settings_);
            fields_[1].text.clear();
            screen_ = Screen::Home;
            check();
        });
    });
}

void Launcher::check() {
    home_ = Home::Checking;
    message_.clear();
    const fs::path install = settings_.install;
    work([this, install] {
        // The machine, told once a run (engine.cpp reportMachine): the site's fleet of cards and
        // drivers, which is what an update has to fit. It is small, and a report that does not
        // land is logged there and holds nothing else up.
        if (!reported_) {
            reported_ = true;
            reportMachine(place_, gpu_, tuned_, who_.token);
        }
        const Answer still = stillSigned(place_, who_);
        if (still == Answer::Wrong) {
            post([this] {
                forget();
                who_ = {};
                screen_ = Screen::SignIn;
                message_ = "Your sign-in has expired. Sign in again.";
            });
            return;
        }
        Manifest manifest;
        std::string error;
        if (still == Answer::Offline || !fetchManifest(place_, manifest, error)) {
            post([this] { home_ = Home::Offline; });
            return;
        }
        std::vector<Entry> todo = stale(install, manifest, progress_);
        const bool fresh = installedVersion(install).empty();
        if (!fresh) refreshShortcut();
        post([this, manifest, todo, fresh] {
            manifest_ = manifest;
            todo_ = todo;
            todoBytes_ = 0;
            for (const Entry& e : todo_) todoBytes_ += e.size;
            home_ = todo_.empty() ? Home::Ready : fresh ? Home::Install : Home::Update;
            checkedAt_ = Clock::now();
            // An update is fetched without asking: the game a player has is the one a fix has
            // to reach. The first install waits for its button on Windows, where the folder can
            // be changed beside it; the Mac's is always ~/Applications and Linux's always
            // ~/.local/share/MU2/MU Classic, so there it starts at once.
            if (home_ == Home::Update) autoInstall_ = true;
#if defined(__APPLE__) || defined(__linux__)
            if (home_ == Home::Install) autoInstall_ = true;
#endif
            if (home_ == Home::Install) playAfter_ = false;
            if (home_ == Home::Ready && playAfter_) {
                playAfter_ = false;
                start();
            }
        });
    });
}

// Uninstall, once asked twice (the Uninstall link, then its button): the game's files go and the
// launcher stays, so the next check offers Install (engine.h uninstall).
void Launcher::uninstallNow() {
    home_ = Home::Removing;
    message_.clear();
    const fs::path install = settings_.install;
    const Manifest manifest = manifest_;
    work([this, install, manifest] {
        std::string error;
        const bool done = uninstall(install, manifest, error);
        post([this, done, error] {
            if (done) {
                todo_.clear();
                check();
            } else {
                message_ = error;
                home_ = Home::Failed;
            }
        });
    });
}

void Launcher::install() {
    home_ = Home::Downloading;
    message_.clear();
    sampledBytes_ = 0;
    sampledAt_ = Clock::now();
    speed_ = 0.0;
    const fs::path install = settings_.install;
    const Manifest manifest = manifest_;
    const std::vector<Entry> todo = todo_;
    work([this, install, manifest, todo] {
        std::string error;
        const bool done = update(place_, install, manifest, todo, progress_, stop_, error);
        const bool paused = !done && stop_.load();
        post([this, done, paused, error] {
            if (done) {
                todo_.clear();
                home_ = Home::Ready;
                if (playAfter_) {
                    playAfter_ = false;
                    start();
                }
            } else if (paused) {
                home_ = Home::Paused;
            } else {
                message_ = error;
                home_ = Home::Failed;
            }
        });
    });
}

// Play: the release asked about first unless that was a moment ago, so a launcher left open
// since yesterday does not start yesterday's game. Updated if need be, then started.
void Launcher::playNow() {
    if (game_) return;
    if (Clock::now() - checkedAt_ > std::chrono::seconds(30)) {
        playAfter_ = true;
        check();
        return;
    }
    start();
}

// The game's ticket asked for on a thread of its own (the worker may still be finishing the check
// that called this), then the game started with it (engine.h GameTicket).
void Launcher::start() {
    if (starting_ || game_) return;
    starting_ = true;
    message_.clear();
    const Place place = place_;
    const Signed who = who_;
    std::thread([this, place, who] {
        GameTicket ticket;
        std::string message;
        const Answer a = gameTicket(place, who, ticket, message);
        post([this, a, ticket, message] {
            starting_ = false;
            if (a == Answer::Wrong) {
                forget();
                who_ = {};
                screen_ = Screen::SignIn;
                message_ = "Your sign-in has expired. Sign in again.";
                return;
            }
            std::string error;
            unsigned long pid = 0;
            const Region& region = regionNamed(settings_.region);
            core::logf("launcher: playing in %s (%s)", region.name.c_str(), region.server.c_str());
            if (a != Answer::Ok || !play(settings_.install, manifest_, ticket, region.server, error, &pid)) {
                message_ = a != Answer::Ok ? message : error;
                home_ = Home::Failed;
                return;
            }
            game_ = pid;
            sendCrashesAfter(20);
            // The logs as the game starts and the player goes in: a window that opened behind,
            // a freeze in the load (writeLogDigest). The whole session follows at the next start.
            sendCrashesAfter(150, "launcher.log");
            // Minimised only once the game's window has the front (engine.h handFront).
            std::thread([this, pid] {
                handFront(pid);
                post([this] { glfwIconifyWindow(window_); });
                glfwPostEmptyEvent();
            }).detach();
        });
        glfwPostEmptyEvent();
    }).detach();
}

// What crashed or froze, sent home on a thread of its own after `delay` (launcher/crash_upload.h):
// at the start, and again after Play, so a game that dies while starting is heard from the same
// evening. Copies only, so the window may close while it runs.
void Launcher::sendCrashesAfter(int delay, const char* launcherLog) {
    const std::vector<fs::path> folders = crashFolders(settings_.install);
    const CrashFrom from{place_.site, place_.platform, "launcher", who_.token, gpu_.name, gpu_.driver};
    const fs::path logs = userFolder() / "logs";
    const fs::path log = launcherLog ? logs / launcherLog : fs::path();
    std::thread([folders, from, delay, logs, log] {
        std::this_thread::sleep_for(std::chrono::seconds(delay));
        if (!log.empty()) writeLogDigest(folders, log, logs);
        sendCrashes(folders, from);
    }).detach();
}

void Launcher::logOut() {
    const Signed who = who_;
    who_ = {};
    screen_ = Screen::SignIn;
    message_.clear();
    focus_ = 0;
    fields_[0].text = settings_.email;
    work([this, who] { signOut(place_, who); });
}

void Launcher::openUrl(const std::string& url) {
#ifdef _WIN32
    ShellExecuteW(nullptr, L"open", std::wstring(url.begin(), url.end()).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
#ifdef __linux__
    std::string open = "xdg-open", target = url;  // the desktop's own browser
#else
    std::string open = "/usr/bin/open", target = url;
#endif
    char* argv[] = {open.data(), target.data(), nullptr};
    pid_t pid = 0;
    posix_spawnp(&pid, open.c_str(), nullptr, nullptr, argv, environ);
#endif
}

void Launcher::pickFolder() {
#ifdef _WIN32
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dialog)))) return;
    DWORD flags = 0;
    dialog->GetOptions(&flags);
    dialog->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(L"Install MU Classic in");
    if (SUCCEEDED(dialog->Show(glfwGetWin32Window(window_)))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                fs::path chosen(path);
                // A folder of its own inside what he picked, unless he picked one named so.
                if (chosen.filename() != "MU2" && chosen.filename() != "MU Classic") chosen /= "MU Classic";
                settings_.install = chosen;
                saveSettings(settings_);
                CoTaskMemFree(path);
                check();
            }
            item->Release();
        }
    }
    dialog->Release();
#endif
}

// ---- drawing ------------------------------------------------------------------------------------

// A rounded box, its colour running from `top` to `foot`, as one fan.
void Launcher::panel(const Box& b, float radius, uint32_t top, uint32_t foot) {
    const float r = std::min(radius, std::min(b.w, b.h) * 0.5f);
    constexpr int kArc = 5;
    float xy[(kArc + 1) * 4 * 2];
    uint32_t colour[(kArc + 1) * 4];
    const float cx[4] = {b.right() - r, b.x + r, b.x + r, b.right() - r};
    const float cy[4] = {b.y + r, b.y + r, b.bottom() - r, b.bottom() - r};
    int n = 0;
    for (int corner = 0; corner < 4; ++corner) {
        for (int i = 0; i <= kArc; ++i) {
            const float a = (float(corner) * 90.0f + 90.0f * float(i) / float(kArc)) * 3.14159265f / 180.0f;
            const float x = cx[corner] + std::cos(a) * r, y = cy[corner] - std::sin(a) * r;
            xy[n * 2] = x;
            xy[n * 2 + 1] = y;
            const float t = b.h > 0 ? std::clamp((y - b.y) / b.h, 0.0f, 1.0f) : 0.0f;
            auto mixed = [t](uint32_t a8, uint32_t b8, int shift) {
                const float av = float((a8 >> shift) & 255), bv = float((b8 >> shift) & 255);
                return uint32_t(std::lround(av + (bv - av) * t)) << shift;
            };
            colour[n] = mixed(top, foot, 0) | mixed(top, foot, 8) | mixed(top, foot, 16) | mixed(top, foot, 24);
            ++n;
        }
    }
    canvas_.polygon(xy, colour, n);
}

float Launcher::measure(const std::string& s, float size, bool bold) const {
    const gfx::Face& face = bold ? word_ : body_;
    return face.measure(size * s_, s) / s_;
}

void Launcher::words(float x, float baseline, const std::string& s, uint32_t ink, float size, bool bold) {
    const gfx::Face& face = bold ? word_ : body_;
    canvas_.lettered(face, bold ? wordTex_ : bodyTex_, std::round(x * s_), std::round(baseline * s_), size * s_, 0.0f,
                     ink, s);
}

// The game's slab (controls.cpp slabOf): glow, rim, body, the light along its top, the ember.
bool Launcher::button(const Box& b, const std::string& word, bool primary, bool off, float size) {
    const int index = widgetIndex_++;
    const bool hot = !off && over(px(b));
    const bool held = hot && down_ && pressedAt_ == index;
    if (hot && clicked_) pressedAt_ = index;
    if (hot) wantCursor_ = hand_;
    const Look& look = off ? kOff : primary ? (held ? kPrimaryHeld : hot ? kPrimaryOver : kPrimaryRest)
                                            : (held ? kOff : hot ? kSecondaryOver : kSecondaryRest);
    const Box at = px({b.x, b.y + (held ? 1.0f : 0.0f), b.w, b.h});
    const float r = 3.0f * s_, line = std::max(1.0f, std::round(s_));
    if (look.glow > 0.0f) {
        for (const float spread : {8.0f, 4.0f}) {
            panel(at.grown(line + spread * s_), r + spread * s_, tone(kBlood, look.glow * 0.10f), tone(kBlood, look.glow * 0.16f));
        }
    }
    if (!held && !off) panel({at.x, at.y + 3.0f * s_, at.w, at.h}, r, hex(0, 0.0f), hex(0, 0.55f));
    panel(at.grown(line), r + line, tone(look.rimTop), tone(look.rimFoot));
    panel(at, r, tone(look.top), tone(look.foot));
    if (look.highlight > 0.0f) canvas_.rect({at.x + r, at.y, at.w - r * 2.0f, line}, gfx::rgba(1.0f, 0.75f, 0.68f, look.highlight));
    if (look.ember > 0.0f) {
        const float tall = std::round(at.h * 0.62f);
        canvas_.shade({at.x, at.bottom() - tall, at.w, tall}, tone(kBlood, 0.0f), tone(kBlood, 0.0f), tone(kBlood, look.ember),
                      tone(kBlood, look.ember));
    }
    const float wide = measure(word, size, true);
    const float baseline = b.y + (held ? 1.0f : 0.0f) + (b.h + word_.ascent(size * s_) / s_ * 0.66f) * 0.5f;
    words(b.midX() - wide * 0.5f + 0.0f, baseline + 1.0f, word, hex(0, 0.75f), size, true);
    words(b.midX() - wide * 0.5f, baseline, word, tone(look.ink), size, true);
    return !off && hot && released_ && pressedAt_ == index;
}

// A flat word at the top right ("Edit account", "Log out"): its right edge at `right`.
bool Launcher::flat(float right, float y, const std::string& word, float* width) {
    const float wide = measure(word, 15.0f) + 20.0f;
    if (width) *width = wide;
    const Box b{right - wide, y, wide, 32.0f};
    const int index = widgetIndex_++;
    const bool hot = over(px(b));
    if (hot && clicked_) pressedAt_ = index;
    if (hot) {
        wantCursor_ = hand_;
        panel(px(b), 3.0f * s_, hex(0x0D0B0A, 0.55f), hex(0x0D0B0A, 0.55f));
    }
    words(b.x + 10.0f, b.y + 21.0f, word, hex(hot ? kBoneHi : 0xB5AB9B), 15.0f);
    return hot && released_ && pressedAt_ == index;
}

bool Launcher::link(float x, float baseline, const std::string& word, uint32_t ink, float size) {
    const float wide = measure(word, size);
    const Box b{x, baseline - size, wide, size * 1.4f};
    const int index = widgetIndex_++;
    const bool hot = over(px(b));
    if (hot && clicked_) pressedAt_ = index;
    if (hot) wantCursor_ = hand_;
    words(x, baseline, word, hex(hot ? kBoneHi : ink), size);
    return hot && released_ && pressedAt_ == index;
}

void Launcher::field(const Box& b, Field& f, int index, const char* placeholder) {
    const Box at = px(b);
    const bool hot = over(at);
    if (hot) wantCursor_ = beam_;
    if (hot && clicked_) focus_ = index;
    const bool focused = focus_ == index && screen_ == Screen::SignIn;
    canvas_.rect(at, hex(focused ? 0xA88B6C : 0x241B15));
    canvas_.rect(at.grown(-std::max(1.0f, std::round(s_))), hex(0x050303));
    const std::string shown = f.secret ? std::string(f.text.size(), char(gfx::Face::kMiddleDot)) : f.text;
    const float baseline = b.y + b.h * 0.5f + 5.5f;
    if (shown.empty() && !focused) {
        words(b.x + 14.0f, baseline, placeholder, hex(kQuiet), 15.0f);
    } else {
        if (shown.empty()) words(b.x + 14.0f, baseline, placeholder, hex(kQuiet, 0.5f), 15.0f);
        // The end of what he typed stays in sight in a field too narrow for it.
        const float room = b.w - 28.0f;
        std::string visible = shown;
        while (!visible.empty() && measure(visible, 15.0f) > room) visible.erase(0, 1);
        words(b.x + 14.0f, baseline, visible, hex(kBone), 15.0f);
        const bool lit = std::fmod(std::chrono::duration<double>(Clock::now() - started_).count(), 1.0) < 0.55;
        if (focused && lit) {
            const float caret = b.x + 14.0f + measure(visible, 15.0f) + 1.0f;
            canvas_.rect(px({caret, b.y + 12.0f, 1.5f, b.h - 24.0f}), hex(0xF06150));
        }
    }
}

// The game's warning: white words on a red box (memory: warnings are white on red).
void Launcher::alert(const Box& b, const std::string& message) {
    panel(px(b), 3.0f * s_, hex(0x9E2A1F), hex(0x9E2A1F));
    words(b.x + 14.0f, b.y + b.h * 0.5f + 5.0f, message, hex(0xFFFFFF), 15.0f);
}

void Launcher::drawChrome() {
    // The window's own buttons, and the strip across its top that moves it. Linux's at the right,
    // as Windows' are and most Linux desktops' too.
#if defined(_WIN32) || defined(__linux__)
    const Box close{kW - 46.0f, 0, 46.0f, 32.0f}, minimise{kW - 92.0f, 0, 46.0f, 32.0f};
    for (int which = 0; which < 2; ++which) {
        const Box b = which == 0 ? minimise : close;
        const bool hot = over(px(b));
        if (hot) canvas_.rect(px(b), which == 1 ? hex(0xC42B1C) : hex(0xFFFFFF, 0.08f));
        const uint32_t ink = hex(hot && which == 1 ? 0xFFFFFF : kLabel);
        if (which == 0) {
            canvas_.rect(px({b.midX() - 5.0f, b.midY(), 10.0f, 1.0f}), ink);
        } else {
            for (int i = 0; i < 10; ++i) {
                canvas_.rect(px({b.midX() - 5.0f + float(i), b.midY() - 5.0f + float(i), 1.2f, 1.2f}), ink);
                canvas_.rect(px({b.midX() + 4.0f - float(i), b.midY() - 5.0f + float(i), 1.2f, 1.2f}), ink);
            }
        }
        if (hot && released_) {
            if (which == 0) glfwIconifyWindow(window_);
            else glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
    }
#else
    const uint32_t lights[3] = {0xFF5F57, 0xFEBC2E, 0x3A3631};
    for (int i = 0; i < 3; ++i) {
        const Box b{16.0f + float(i) * 20.0f, 14.0f, 12.0f, 12.0f};
        panel(px(b), 6.0f * s_, hex(lights[i]), hex(lights[i]));
        if (i < 2 && over(px(b.grown(3.0f))) && released_) {
            if (i == 0) glfwSetWindowShouldClose(window_, GLFW_TRUE);
            else glfwIconifyWindow(window_);
        }
    }
#endif
}

void Launcher::drawSignIn() {
    // The wallpaper dark to the left under the form (the canvas's sign-in: a gradient, no panel).
    canvas_.shade(px({0, 0, 270, kH}), hex(0x0D0B0A, 0.92f), hex(0x0D0B0A, 0.8f), hex(0x0D0B0A, 0.8f), hex(0x0D0B0A, 0.92f));
    canvas_.shade(px({270, 0, 330, kH}), hex(0x0D0B0A, 0.8f), hex(0x0D0B0A, 0.0f), hex(0x0D0B0A, 0.0f), hex(0x0D0B0A, 0.8f));
    const float x = 40.0f, w = 280.0f;
    const float logoW = 176.0f, logoH = logoW * logo_.height / std::max(1.0f, logo_.width);
    float y = 76.0f - (message_.empty() ? 0.0f : 28.0f);
    canvas_.image(logo_, px({x, y, logoW, logoH}));
    y += logoH + 32.0f;
    field({x, y, w, 44.0f}, fields_[0], 0, "Email");
    y += 56.0f;
    field({x, y, w, 44.0f}, fields_[1], 1, "Password");
    y += 56.0f;
    // Stay signed in, and the way to a new password.
    const Box tick{x, y + 8.0f, 16.0f, 16.0f};
    const Box tickRow{x, y, 120.0f, 32.0f};
    const bool tickHot = over(px(tickRow));
    if (tickHot) wantCursor_ = hand_;
    canvas_.rect(px(tick), hex(stay_ ? 0xC2362B : 0x6B5645));
    canvas_.rect(px(tick.grown(-1.0f)), hex(stay_ ? 0xC2362B : 0x050303));
    if (stay_) {
        for (int i = 0; i < 4; ++i) canvas_.rect(px({x + 3.5f + float(i), y + 15.5f + float(i), 1.6f, 1.6f}), hex(0xFFFFFF));
        for (int i = 0; i < 7; ++i) canvas_.rect(px({x + 6.5f + float(i), y + 18.0f - float(i), 1.6f, 1.6f}), hex(0xFFFFFF));
    }
    if (tickHot && released_) stay_ = !stay_;
    words(x + 26.0f, y + 21.0f, "Stay signed in", hex(kLabel), 15.0f);
    const float forgot = measure("Forgot password?", 15.0f);
    if (link(x + w - forgot, y + 21.0f, "Forgot password?", kQuiet)) openUrl(place_.web + "/account");
    y += 44.0f;
    if (button({x, y, w, 56.0f}, busy_ ? "Signing in..." : "Sign in", true, busy_) || (enter_ && !busy_)) signIn();
    y += 56.0f + 16.0f;
    if (!message_.empty()) alert({x, y, w, 44.0f}, message_);
    words(x, kH - 40.0f, "New to MU?", hex(kQuiet), 15.0f);
    if (link(x + measure("New to MU?", 15.0f) + 5.0f, kH - 40.0f, "Create account", kLabel)) openUrl(place_.web + "/");
}

void Launcher::drawHome() {
    // The wallpaper falls to dark under the logo and the button.
    canvas_.shade(px({0, kH * 0.45f, kW, kH * 0.45f}), hex(0x0D0B0A, 0.0f), hex(0x0D0B0A, 0.0f), hex(0x0D0B0A, 0.95f),
                  hex(0x0D0B0A, 0.95f));
    canvas_.rect(px({0, kH * 0.9f, kW, kH * 0.1f + 1.0f}), hex(0x0D0B0A, 0.95f));
#if defined(_WIN32) || defined(__linux__)
    const float right = kW - 100.0f, top = 0.0f;
#else
    const float right = kW - 30.0f, top = 4.0f;
#endif
    float logOutWide = 0;
    if (flat(right, top, "Log out", &logOutWide)) {
        logOut();
        return;
    }
    float editWide = 0;
    if (flat(right - logOutWide - 4.0f, top, "Edit account", &editWide)) openUrl(place_.web + "/account");
    dragRight_ = right - logOutWide - editWide - 4.0f;

    const float x = 40.0f;
    const float logoW = 160.0f, logoH = logoW * logo_.height / std::max(1.0f, logo_.width);
    const float rowY = kH - 40.0f - 56.0f;
    canvas_.image(logo_, px({x, rowY - 24.0f - logoH, logoW, logoH}));
    const Box main{x, rowY, 200.0f, 56.0f};
    const float textX = main.right() + 20.0f;
    auto gb = [](uint64_t bytes) {
        char s[32];
        std::snprintf(s, sizeof s, bytes >= 1000000000ull ? "%.2f GB" : "%.0f MB",
                      bytes >= 1000000000ull ? double(bytes) / 1e9 : double(bytes) / 1e6);
        return std::string(s);
    };
    // Uninstall at the row's right, where a game is there to remove and nothing is under way.
    const auto offerUninstall = [&] {
        const std::string what = "Uninstall";
        if (link(kW - 40.0f - measure(what, 15.0f), rowY + 33.0f, what, kQuiet)) home_ = Home::Uninstall;
    };
    switch (home_) {
        case Home::Checking: {
            button(main, "Install", true, true);
            const uint64_t total = std::max<uint64_t>(1, progress_.total.load());
            const int pct = int(100.0 * double(progress_.done.load()) / double(total));
            words(textX, rowY + 34.0f, working_ && progress_.total.load() > 0 ? "Checking your files... " + std::to_string(pct) + "%"
                                                                            : "Checking for updates...",
                  hex(kQuiet), 15.0f);
            break;
        }
        case Home::Install:
        case Home::Update: {
            const bool fresh = home_ == Home::Install;
            if (button(main, fresh ? "Install" : "Update", true)) install();
            words(textX, rowY + 24.0f, (fresh ? gb(todoBytes_) + " download" : "Update ready, " + gb(todoBytes_)), hex(kBone), 15.0f);
            const std::string folder = utf8Of(settings_.install);
            words(textX, rowY + 46.0f, folder, hex(kQuiet), 15.0f);
#ifdef _WIN32
            if (fresh && link(textX + measure(folder, 15.0f) + 8.0f, rowY + 46.0f, "Change", kLabel)) pickFolder();
#endif
            if (!fresh) offerUninstall();
            break;
        }
        case Home::Downloading:
        case Home::Paused: {
            const bool paused = home_ == Home::Paused;
            const uint64_t done = progress_.done.load(), total = std::max<uint64_t>(1, progress_.total.load());
            const auto now = Clock::now();
            const double dt = std::chrono::duration<double>(now - sampledAt_).count();
            if (dt >= 1.0) {
                const double rate = double(done - std::min(done, sampledBytes_)) / dt;
                speed_ = speed_ <= 0.0 ? rate : speed_ * 0.6 + rate * 0.4;
                sampledBytes_ = done;
                sampledAt_ = now;
            }
            const int pct = int(100.0 * double(done) / double(total));
            const float barX = x, barW = 400.0f;
            words(barX, rowY + 18.0f, (paused ? "Paused - " : "Downloading - ") + std::to_string(pct) + "%", hex(kBoneHi), 15.0f);
            if (!paused && speed_ > 0.0) {
                const double left = double(total - std::min(total, done)) / speed_;
                char line[64];
                std::snprintf(line, sizeof line, "%.0f MB/s - %s left", speed_ / 1e6,
                              left < 90 ? (std::to_string(int(left)) + " s").c_str() : (std::to_string(int(left / 60 + 0.5)) + " min").c_str());
                const float wide = measure(line, 15.0f);
                words(barX + barW - wide, rowY + 18.0f, line, hex(kQuiet), 15.0f);
            }
            const Box bar{barX, rowY + 30.0f, barW, 8.0f};
            canvas_.rect(px(bar), hex(0x241B15));
            canvas_.rect(px(bar.grown(-1.0f)), hex(0x050303));
            const float fill = (bar.w - 2.0f) * float(std::min(1.0, double(done) / double(total)));
            if (fill > 0.5f) panel(px({bar.x + 1.0f, bar.y + 1.0f, fill, bar.h - 2.0f}), 1.0f, hex(0xD4493B), hex(0x7A1A13));
            const Box pause{barX + barW + 16.0f, rowY + 6.0f, 44.0f, 44.0f};
            if (button(pause, paused ? ">" : "||", false, false, 16.0f)) {
                if (paused) install();
                else stop_ = true;
            }
            break;
        }
        case Home::Ready: {
            if (button(main, game_ ? "In game" : starting_ ? "Starting..." : "Play", true, starting_ || game_, 20.0f)) playNow();
            panel(px({textX, rowY + 11.0f, 8.0f, 8.0f}), 4.0f * s_, hex(0x7FD66A), hex(0x7FD66A));
            words(textX + 16.0f, rowY + 20.0f, "Up to date", hex(kQuiet), 15.0f);
            // Where Play goes, and the way to another, beside Play (the user, 2026-10-17: "so
            // users see the region", "has to be ... near button not on top").
            drawRegion(textX + 16.0f, rowY + 41.0f);
            if (tuned_) {
                words(textX + 16.0f, rowY + 59.0f,
                      std::string("Graphics set to ") + firstRunSet(gpu_).name + " for this PC",
                      hex(kQuiet), 13.0f);
            }
            if (!starting_ && !game_) offerUninstall();
            break;
        }
        case Home::Uninstall: {
            if (button(main, "Uninstall", true)) uninstallNow();
            const Box cancel{main.right() + 12.0f, rowY, 130.0f, 56.0f};
            if (button(cancel, "Cancel", false)) check();
            const float askX = cancel.right() + 20.0f;
            words(askX, rowY + 24.0f, "Remove the game from this computer?", hex(kBone), 15.0f);
            words(askX, rowY + 46.0f, "You stay signed in. Install again here any time.", hex(kQuiet), 15.0f);
            break;
        }
        case Home::Removing: {
            button(main, "Uninstalling...", true, true);
            words(textX, rowY + 34.0f, "Removing the game's files...", hex(kQuiet), 15.0f);
            break;
        }
        case Home::Offline:
        case Home::Failed: {
            if (button(main, "Retry", false)) check();
            alert({textX, rowY + 6.0f, 520.0f, 44.0f},
                  home_ == Home::Offline ? "Can't reach the server. Check your connection." : message_);
            break;
        }
    }
    // The machine along the top: the adapter and the driver the game will run on, and the warning
    // when the driver is the thing in the way (docs/plans/launcher.md, L6).
#if defined(_WIN32) || defined(__linux__)
    const float machineX = x;  // nothing of the window's own at the left of the strip
#else
    const float machineX = 80.0f;  // past the window's own lights, which take the first 68
#endif
    if (gpu_.known) words(machineX, 26.0f, gpu_.line(), hex(kQuiet), 13.0f);
    // No Vulkan is no longer in the way: the game draws through Direct3D 11 then (gfx/window.cpp).
    // No driver, or an old one (driver.h): said beside the card, quietly, and nothing more (the
    // user, 2026-10-17: "simple elegant message at top left corner near graphics card", "without
    // link, just message").
    if (needsDriver(gpu_)) {
        float at = machineX + measure(gpu_.line(), 13.0f) + 18.0f;
        panel(px({at, 18.0f, 6.0f, 6.0f}), 3.0f * s_, hex(kAmber), hex(kAmber));
        at += 13.0f;
        const std::string say = driverMissing(gpu_) ? "No graphics driver" : "Graphics driver is old";
        words(at, 26.0f, say, hex(kAmber), 13.0f);
    }
    if (!manifest_.version.empty()) {
        const std::string version = "Client " + manifest_.version;
        words(kW - 40.0f - measure(version, 13.0f), kH - 40.0f, version, hex(kQuiet), 13.0f);
    }
    // Last, over whatever lies under it.
    // Only on Ready, where its line beside Play is (drawRegion); closed by any other screen.
    if (home_ != Home::Ready) regionsOpen_ = false;
    if (regionsOpen_) drawRegionList();
}

// Every region's round trip, on a thread of its own; then, unless he picked one himself, the
// quickest is where Play goes (engine.h Region). Kept in launcher.json, so a run that starts Play
// before its pings are in plays where the last one found quickest.
void Launcher::measureRegions() {
    pinger_ = std::thread([this] {
        std::vector<double> pings;
        for (const Region& one : regions()) pings.push_back(pingMs(one));
        post([this, pings] {
            pings_ = pings;
            std::string said;
            for (size_t i = 0; i < pings.size(); ++i) {
                char one[64];
                std::snprintf(one, sizeof one, "%s%s %.0f ms", i ? ", " : "", regions()[i].name.c_str(), pings[i]);
                said += one;
            }
            core::logf("launcher: regions %s", said.c_str());
            const std::string best = quickest(pings);
            if (!settings_.regionPicked && !best.empty() && best != settings_.region) {
                settings_.region = best;
                saveSettings(settings_);
                core::logf("launcher: region %s, the quickest from here", regionNamed(best).name.c_str());
            }
        });
        glfwPostEmptyEvent();
    });
}

// **The region** (engine.h Region) beside Play: "Region: North Europe (34 ms)" and a caret, its
// words' baseline at `baseline`, that opens the list above it.
void Launcher::drawRegion(float x, float baseline) {
    const auto& all = regions();
    const Region& region = regionNamed(settings_.region);
    size_t at = 0;
    while (at < all.size() && all[at].id != region.id) ++at;
    std::string word = "Region: " + region.name;
    if (at < pings_.size() && pings_[at] >= 0.0) word += " (" + std::to_string(int(pings_[at] + 0.5)) + " ms)";
    const float wide = measure(word, 15.0f) + 20.0f + 14.0f;
    const Box b{x - 10.0f, baseline - 19.0f, wide, 26.0f};
    regionAt_ = b;
    const int index = widgetIndex_++;
    const bool hot = !starting_ && over(px(b));
    if (hot && clicked_) pressedAt_ = index;
    if (hot) wantCursor_ = hand_;
    if (hot || regionsOpen_) panel(px(b), 3.0f * s_, hex(0x0D0B0A, 0.55f), hex(0x0D0B0A, 0.55f));
    const uint32_t ink = hex(hot || regionsOpen_ ? kBoneHi : kBone);
    words(x, baseline, word, ink, 15.0f);
    // The caret: a small V of squares, as the window's own close draws its X.
    const float cx = b.right() - 17.0f, cy = baseline - 7.0f;
    for (int i = 0; i < 4; ++i) {
        canvas_.rect(px({cx + float(i), cy + float(i), 1.2f, 1.2f}), ink);
        canvas_.rect(px({cx + 6.0f - float(i), cy + float(i), 1.2f, 1.2f}), ink);
    }
    if (hot && released_ && pressedAt_ == index) regionsOpen_ = !regionsOpen_;
}

// The regions above the line beside Play: each one's name and its server's city, a tick on the one kept,
// and what a region means for who he meets. A press anywhere else closes it.
void Launcher::drawRegionList() {
    constexpr float kRow = 48.0f, kWide = 280.0f, kFoot = 70.0f;
    const auto& all = regions();
    const float tall = kRow * float(all.size()) + kFoot;
    // Its foot clear of "Up to date", the line above the region's.
    const Box list{std::min(regionAt_.x, kW - 20.0f - kWide), regionAt_.y - 30.0f - tall, kWide, tall};
    panel(px(list.grown(1.0f)), 4.0f * s_, hex(0x6B5645), hex(0x2B2219));
    panel(px(list), 3.0f * s_, hex(0x16110E, 0.98f), hex(0x0D0B0A, 0.98f));
    for (size_t i = 0; i < all.size(); ++i) {
        const Region& one = all[i];
        const Box row{list.x, list.y + kRow * float(i), list.w, kRow};
        const int index = widgetIndex_++;
        const bool hot = over(px(row.grown(-2.0f)));
        if (hot && clicked_) pressedAt_ = index;
        if (hot) {
            wantCursor_ = hand_;
            panel(px(row.grown(-3.0f)), 3.0f * s_, hex(0xFFFFFF, 0.06f), hex(0xFFFFFF, 0.06f));
        }
        const bool kept = one.id == settings_.region;
        words(row.x + 16.0f, row.y + 22.0f, one.name, hex(hot ? 0xFFFFFF : kBoneHi), 15.0f, true);
        words(row.x + 16.0f, row.y + 39.0f, "Server in " + one.city, hex(kQuiet), 13.0f);
        // Its round trip from here, left of the tick: "..." until measured.
        {
            char ms[32];
            if (i >= pings_.size()) std::snprintf(ms, sizeof ms, "...");
            else if (pings_[i] < 0.0) std::snprintf(ms, sizeof ms, "no answer");
            else std::snprintf(ms, sizeof ms, "%.0f ms", pings_[i]);
            words(row.right() - 44.0f - measure(ms, 13.0f), row.y + 30.0f, ms, hex(kQuiet), 13.0f);
        }
        if (kept) {
            const float tx = row.right() - 30.0f, ty = row.y + 20.0f;
            for (int k = 0; k < 4; ++k) canvas_.rect(px({tx + float(k), ty + float(k), 1.6f, 1.6f}), hex(0xD4493B));
            for (int k = 0; k < 7; ++k) canvas_.rect(px({tx + 3.0f + float(k), ty + 3.0f - float(k), 1.6f, 1.6f}), hex(0xD4493B));
        }
        if (hot && released_ && pressedAt_ == index) {
            // His pick, even of the one the pings chose: from here on the pings only inform.
            if (!kept || !settings_.regionPicked) {
                settings_.region = one.id;
                settings_.regionPicked = true;
                saveSettings(settings_);
                core::logf("launcher: region %s (%s), picked", one.name.c_str(), one.server.c_str());
            }
            regionsOpen_ = false;
        }
    }
    const float footY = list.y + kRow * float(all.size());
    canvas_.rect(px({list.x + 12.0f, footY, list.w - 24.0f, 1.0f}), hex(0x3A2E25));
    words(list.x + 16.0f, footY + 22.0f, "Each region is its own world.", hex(kQuiet), 13.0f);
    words(list.x + 16.0f, footY + 39.0f, "You meet players of the same region.", hex(kQuiet), 13.0f);
    words(list.x + 16.0f, footY + 57.0f,
          settings_.regionPicked ? "Your choice." : "Chosen for you: the quickest from here.", hex(kQuiet), 13.0f);
    // A press off the list and off the word closes it.
    if (clicked_ && !over(px(list)) && !over(px(regionAt_))) regionsOpen_ = false;
}

void Launcher::draw() {
    widgetIndex_ = 0;
    wantCursor_ = nullptr;
    canvas_.clear();
    canvas_.image(wallpaper_, px({0, 0, kW, kH}));
    if (screen_ == Screen::SignIn) {
        dragRight_ = kW - 260.0f;
        drawSignIn();
    } else {
        drawHome();
    }
    drawChrome();
    glfwSetCursor(window_, wantCursor_);
}

// ---- the window ---------------------------------------------------------------------------------

bx::DefaultAllocator g_allocator;

gfx::Art loadArt(const char* name) {
    gfx::Art art;
    std::vector<uint8_t> bytes;
    if (!readEmbedded(name, bytes)) return art;
    bimg::ImageContainer* image = bimg::imageParse(&g_allocator, bytes.data(), uint32_t(bytes.size()), bimg::TextureFormat::RGBA8);
    if (!image) return art;
    art.handle = bgfx::createTexture2D(uint16_t(image->m_width), uint16_t(image->m_height), false, 1,
                                       bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_UVW_CLAMP,
                                       bgfx::copy(image->m_data, image->m_size));
    art.width = float(image->m_width);
    art.height = float(image->m_height);
    bimg::imageFree(image);
    return art;
}

Launcher* g_self = nullptr;

bool Launcher::open() {
    core::setFileSource(&kInside);
#ifdef _WIN32
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // for the folder picker
#endif
#ifdef __linux__
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    if (!options_.shot.empty()) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window_ = glfwCreateWindow(int(kW), int(kH), "MU Classic", nullptr, nullptr);
    if (!window_) return false;
    if (GLFWmonitor* monitor = glfwGetPrimaryMonitor()) {
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        int w = 0, h = 0;
        glfwGetWindowSize(window_, &w, &h);
        if (mode) glfwSetWindowPos(window_, (mode->width - w) / 2, (mode->height - h) / 2);
    }
    hand_ = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
    beam_ = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
    g_self = this;
    glfwSetCharCallback(window_, [](GLFWwindow*, unsigned int code) {
        if (code >= 32 && code < 127) g_self->typed_ += char(code);
    });
    glfwSetKeyCallback(window_, [](GLFWwindow*, int key, int, int action, int mods) {
        if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
        if (key == GLFW_KEY_BACKSPACE) ++g_self->backspaces_;
        else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) g_self->enter_ = true;
        else if (key == GLFW_KEY_TAB) g_self->tab_ = true;
        else if (key == GLFW_KEY_V && (mods & (GLFW_MOD_CONTROL | GLFW_MOD_SUPER))) g_self->paste_ = true;
    });
    glfwGetFramebufferSize(window_, &fbW_, &fbH_);

    bgfx::Init init;
#ifdef _WIN32
    // As the game's first run will: Direct3D 11 where the probe said so (gpu.h rendererFor), or
    // where --renderer asks. Otherwise Vulkan, and Direct3D 11 if that does not start.
    const int renderer = options_.renderer >= 0 ? options_.renderer : gpu_.renderer;
    init.type = renderer == 2 ? bgfx::RendererType::Direct3D11 : bgfx::RendererType::Vulkan;
#elif defined(__linux__)
    init.type = bgfx::RendererType::Vulkan;
#else
    init.type = bgfx::RendererType::Metal;
#endif
    // That renderer or the one named below, never bgfx's own pick (gfx/window.cpp says why).
    init.fallback = false;
    // The window on the swap chain, taken from init's own so its formats stay (gfx/window.cpp).
    chain_ = init.swapChain;
#ifdef _WIN32
    chain_.nwh = glfwGetWin32Window(window_);
#elif defined(__linux__)
    chain_.nwh = reinterpret_cast<void*>(uintptr_t(glfwGetX11Window(window_)));
    chain_.ndt = glfwGetX11Display();
#else
    chain_.nwh = glfwGetCocoaWindow(window_);
#endif
    chain_.width = uint32_t(fbW_);
    chain_.height = uint32_t(fbH_);
    init.swapChain = chain_;
    init.reset = BGFX_RESET_VSYNC;
    init.callback = &callback_;
    bgfx::renderFrame();
    bool up = bgfx::init(init);
#ifdef _WIN32
    // Direct3D 11 where Vulkan does not start, as the game does (gfx/window.cpp).
    if (!up && renderer == 0) {
        core::logError("launcher: Vulkan did not start; trying Direct3D 11");
        init.type = bgfx::RendererType::Direct3D11;
        bgfx::renderFrame();
        up = bgfx::init(init);
    }
#endif
    if (!up) return false;
    bgfxUp_ = true;
    core::logf("launcher: drawn with %s", bgfx::getRendererName(bgfx::getRendererType()));
    if (!iface_.init(std::string("embedded/") + gfx::shaderProfile())) return false;
    iface_.adopt(canvas_);
    if (word_.bake("AlegreyaSansSC-Bold.ttf", 48.0f, 512, 4, 1, 0)) wordTex_ = gfx::uploadFace(word_, "launcher words");
    if (body_.bake("AlegreyaSans-Medium.ttf", 48.0f, 512, 4, 1, 0)) bodyTex_ = gfx::uploadFace(body_, "launcher body");
    word_.dropPixels();
    body_.dropPixels();
    wallpaper_ = loadArt("wallpaper.jpg");
    logo_ = loadArt("logo.png");
    return bgfx::isValid(wordTex_) && bgfx::isValid(bodyTex_);
}

void Launcher::close() {
    stop_ = true;
    if (worker_.joinable()) worker_.join();
    net::abandonWaits();  // a ping still waiting on a server gives up at once
    if (pinger_.joinable()) pinger_.join();
    // With no Vulkan, bgfx never started: shutting it down then crashed the launcher before its
    // box could say why (Wine with vulkan-1.dll off, 2026-10-15).
    if (bgfxUp_) {
        for (bgfx::TextureHandle t : {wordTex_, bodyTex_, wallpaper_.handle, logo_.handle}) {
            if (bgfx::isValid(t)) bgfx::destroy(t);
        }
        iface_.shutdown();
        bgfx::shutdown();
        bgfxUp_ = false;
    }
    if (window_) glfwDestroyWindow(window_);
    window_ = nullptr;
    glfwTerminate();
}

void Launcher::frame() {
    // What the worker finished, applied here on the screen's thread.
    std::vector<std::function<void()>> then;
    {
        std::lock_guard<std::mutex> hold(posted_);
        then.swap(then_);
    }
    for (auto& f : then) f();
    // The game still running keeps Play off; no update is fetched over it meanwhile, the one found
    // waits (autoInstall_) until it has closed.
    if (game_ && !gameRunning(game_)) {
        core::logf("launcher: the game closed");
        game_ = 0;
    }
    // What the worker's answers asked for, once the worker is free to take it.
    if (!working_.load() && !game_) {
        constexpr auto kRecheck = std::chrono::minutes(10);
        if (checkPending_) {
            checkPending_ = false;
            if (screen_ == Screen::Home) check();
        } else if (autoInstall_ && (home_ == Home::Update || home_ == Home::Install)) {
            autoInstall_ = false;
            install();
        } else if (screen_ == Screen::Home && home_ == Home::Ready && Clock::now() - checkedAt_ > kRecheck) {
            check();
        }
    }

    // The pointer, in framebuffer pixels; a press on the top strip that is no button moves the window.
    double cx = 0, cy = 0;
    glfwGetCursorPos(window_, &cx, &cy);
    int ww = 1, wh = 1;
    glfwGetWindowSize(window_, &ww, &wh);
    glfwGetFramebufferSize(window_, &fbW_, &fbH_);
    mx_ = float(cx * double(fbW_) / double(ww));
    my_ = float(cy * double(fbH_) / double(wh));
    s_ = float(fbW_) / kW;
    const bool now = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    clicked_ = now && !down_;
    released_ = !now && down_;
    down_ = now;
    if (clicked_ && my_ < 40.0f * s_ && mx_ < dragRight_ * s_ && mx_ > 76.0f * s_ && !regionsOpen_) {
        dragging_ = true;
        dragX_ = cx;
        dragY_ = cy;
    }
    if (dragging_) {
        if (!down_) {
            dragging_ = false;
        } else {
            int x = 0, y = 0;
            glfwGetWindowPos(window_, &x, &y);
            glfwSetWindowPos(window_, x + int(cx - dragX_), y + int(cy - dragY_));
        }
    }

    // Typing goes to the focused field on the sign-in.
    if (screen_ == Screen::SignIn && !busy_) {
        Field& f = fields_[focus_];
        if (paste_) {
            if (const char* clip = glfwGetClipboardString(window_)) {
                for (const char* c = clip; *c; ++c) {
                    if (*c >= 32 && *c < 127) f.text += *c;
                }
            }
        }
        f.text += typed_;
        for (; backspaces_ > 0 && !f.text.empty(); --backspaces_) f.text.pop_back();
        if (tab_) focus_ = 1 - focus_;
        if (enter_ && focus_ == 0 && fields_[1].text.empty()) {
            focus_ = 1;
            enter_ = false;
        }
    }
    if (screen_ == Screen::Home && enter_ && home_ == Home::Ready && !starting_) playNow();
    typed_.clear();
    backspaces_ = 0;
    tab_ = paste_ = false;

    draw();
    enter_ = false;
    if (released_) pressedAt_ = -1;

    if (chain_.width != uint32_t(fbW_) || chain_.height != uint32_t(fbH_)) {
        chain_.width = uint32_t(fbW_);
        chain_.height = uint32_t(fbH_);
        bgfx::reset(BGFX_RESET_VSYNC, &chain_);
    }
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR, 0x0D0B0AFF);
    bgfx::setViewRect(0, 0, 0, uint16_t(fbW_), uint16_t(fbH_));
    bgfx::touch(0);
    iface_.begin(fbW_, fbH_);
    iface_.add(canvas_);
    iface_.submit(0);
    bgfx::frame();
}

int Launcher::run() {
    settings_ = loadSettings();
    fields_[0].text = settings_.email;
    fields_[1].secret = true;
    // What the machine draws with: the line on the home screen, the warning when the driver is in
    // the way, and the first run's graphics (engine.cpp firstRunGraphics).
    gpu_ = probeGpu();
    tuned_ = firstRunGraphics(gpu_);
    if (options_.shot.empty()) measureRegions();
    if (!open()) {
        core::logError("launcher: the window did not open");
        close();
#ifdef _WIN32
        // The launcher draws with Vulkan or Direct3D 11 as the game does: a PC with neither gets
        // this box rather than nothing at all (the Windows audit, 2026-10-15). The home screen's
        // own driver warning cannot be drawn without them.
        if (options_.shot.empty()) {
            MessageBoxW(nullptr,
                        L"MU Classic could not open its window: this PC's graphics driver runs "
                        L"neither Vulkan nor DirectX 11.\n\nUpdate your graphics card's driver "
                        L"(NVIDIA, AMD or Intel, from their site) and open MU2 Launcher again.",
                        L"MU Classic", MB_OK | MB_ICONWARNING);
        }
#elif defined(__linux__)
        // Linux's box is the desktop's own, zenity or kdialog, as the game's (app/alert.cpp).
        if (options_.shot.empty()) {
            const char* text = "MU Classic could not open its window: it needs Vulkan.\n\nInstall your "
                               "distribution's Vulkan driver (Mesa's, or NVIDIA's own) and its loader "
                               "(libvulkan1 or vulkan-loader), then open the launcher again.";
            std::fprintf(stderr, "MU Classic: %s\n", text);
            const char* zenity[] = {"zenity", "--warning", "--no-wrap", "--title", "MU Classic", "--text", text, nullptr};
            const char* kdialog[] = {"kdialog", "--title", "MU Classic", "--sorry", text, nullptr};
            for (const char* const* line : {zenity, kdialog}) {
                pid_t child = 0;
                if (posix_spawnp(&child, line[0], nullptr, nullptr, const_cast<char* const*>(line), environ) != 0) continue;
                int status = 0;
                waitpid(child, &status, 0);
                if (WIFEXITED(status) && WEXITSTATUS(status) != 127) break;
            }
        }
#endif
        return 1;
    }
    if (!options_.shot.empty()) {
        // A screen set up by hand, drawn a few times so its textures are in, then saved.
        const std::string& which = options_.shotScreen;
        manifest_.version = "354bb0d9";
        settings_.install = defaultInstall();
        checkedAt_ = Clock::now();  // or Ready's ten-minute recheck starts a real one in the shot
        if (which == "error") {
            fields_[0].text = "jaanis@example.com";
            message_ = "Wrong email or password.";
        } else if (which != "signin") {
            screen_ = Screen::Home;
            todoBytes_ = 1528000000ull;
            home_ = which == "install"       ? Home::Install
                    : which == "downloading" ? Home::Downloading
                    : which == "uninstall"   ? Home::Uninstall
                                             : Home::Ready;
            progress_.total = todoBytes_;
            progress_.done = todoBytes_ * 38 / 100;
            regionsOpen_ = which == "regions";  // Ready, the region's list open
            pings_ = {34.0, 212.0};
        }
        // The probe is Windows'; a shot taken where there is none shows the line and the first
        // run's message as a Windows machine with this card would see them.
        if (!gpu_.known) {
            gpu_.known = true;
            gpu_.name = "NVIDIA GeForce RTX 3060";
            gpu_.date = "2024-05-31";
            gpu_.dedicated = uint64_t(6) << 30;
            gpu_.vulkan = true;
            gpu_.preset = presetFor(gpu_);
            tuned_ = true;
        }
        // The driver's word beside the card, as an Intel Iris Plus on a 2022 driver sees it.
        if (which == "driver") {
            gpu_ = Gpu{};
            gpu_.known = true;
            gpu_.name = "Intel(R) Iris(R) Plus Graphics";
            gpu_.vendorId = 0x8086;
            gpu_.date = "2022-11-16";
            gpu_.driverOld = true;
            gpu_.vulkan = true;
            gpu_.preset = presetFor(gpu_);
            tuned_ = false;
            home_ = Home::Ready;
        }
        for (int i = 0; i < 6; ++i) frame();
        bgfx::requestScreenShot(BGFX_INVALID_HANDLE, options_.shot.c_str());
        for (int i = 0; i < 4; ++i) frame();
        close();
        return 0;
    }
    Signed kept;
    if (launcher::kept(kept)) {
        who_ = kept;
        screen_ = Screen::Home;
        home_ = Home::Checking;
    } else {
        focus_ = settings_.email.empty() ? 0 : 1;
    }
    // The launcher itself before the game (launcher/self_update.h): a newer one replaces this one
    // and starts, and this window closes; otherwise the game's own check follows.
    cleanAfterSelfUpdate();
    if (options_.selfUpdate) {
        busy_ = true;
        const Place place = place_;
        const std::vector<std::string> args = options_.args;
        work([this, place, args] {
            std::string note;
            const SelfUpdate done = selfUpdate(place, args, note);
            core::logf("launcher: self-update: %s", note.c_str());
            post([this, done] {
                busy_ = false;
                if (done == SelfUpdate::Started) {
                    glfwSetWindowShouldClose(window_, GLFW_TRUE);
                    return;
                }
                checkPending_ = true;
            });
        });
    } else {
        checkPending_ = true;
    }
    // The last run's logs, the launcher's and the game's whole session (writeLogDigest).
    sendCrashesAfter(0, "launcher.log.previous");
    while (!glfwWindowShouldClose(window_)) {
        // Asleep between events, but awake enough for the caret and a moving download.
        glfwWaitEventsTimeout(working_ || home_ == Home::Downloading ? 1.0 / 30.0 : 0.25);
        frame();
    }
    close();
    return 0;
}

}  // namespace

int runUi(const UiOptions& options) {
    Launcher launcher(options);
    return launcher.run();
}

}  // namespace mu::launcher
