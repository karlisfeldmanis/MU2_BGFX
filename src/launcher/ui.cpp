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
#include "launcher/embedded.h"

namespace mu::launcher {

namespace {

using gfx::Box;
using Clock = std::chrono::steady_clock;

constexpr float kW = 960.0f, kH = 540.0f;  // the design's units

// ---- files inside the exe -----------------------------------------------------------------------

std::string leaf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

const Embedded* find(const std::string& path) {
    const std::string name = leaf(path);
    for (const Embedded& e : embedded()) {
        if (name == e.name) return &e;
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

constexpr uint32_t kBoneHi = 0xF3E8D4, kBone = 0xD9CFBD, kLabel = 0xC9BFAE, kQuiet = 0x8F8575, kDanger = 0xE07A6C;

// ---- the window's state -------------------------------------------------------------------------

enum class Screen { SignIn, Home };
enum class Home { Checking, Install, Update, Downloading, Paused, Ready, Offline, Failed };

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
    Box px(const Box& b) const { return {b.x * s_, b.y * s_, b.w * s_, b.h * s_}; }
    bool over(const Box& b) const { return b.has(mx_, my_); }

    // what the player asks
    void signIn();
    void check();
    void install();
    void playNow();
    void logOut();
    void openUrl(const std::string& url);
    void pickFolder();

    // the worker
    void work(std::function<void()> job);
    void post(std::function<void()> then);

    const UiOptions options_;
    Place place_;
    Settings settings_;
    Signed who_;
    Manifest manifest_;
    std::vector<Entry> todo_;
    uint64_t todoBytes_ = 0;

    Screen screen_ = Screen::SignIn;
    Home home_ = Home::Checking;
    std::string message_;  // the sign-in's refusal, or why the home screen failed
    bool busy_ = false;    // the sign-in's worker is out
    bool stay_ = true;

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
        post([this, manifest, todo, fresh] {
            manifest_ = manifest;
            todo_ = todo;
            todoBytes_ = 0;
            for (const Entry& e : todo_) todoBytes_ += e.size;
            home_ = todo_.empty() ? Home::Ready : fresh ? Home::Install : Home::Update;
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
            } else if (paused) {
                home_ = Home::Paused;
            } else {
                message_ = error;
                home_ = Home::Failed;
            }
        });
    });
}

void Launcher::playNow() {
    std::string error;
    if (!play(settings_.install, manifest_, who_, error)) {
        message_ = error;
        home_ = Home::Failed;
        return;
    }
    glfwIconifyWindow(window_);
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
    std::string open = "/usr/bin/open", target = url;
    char* argv[] = {open.data(), target.data(), nullptr};
    pid_t pid = 0;
    posix_spawn(&pid, open.c_str(), nullptr, nullptr, argv, environ);
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
    // The window's own buttons, and the strip across its top that moves it.
#ifdef _WIN32
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
    if (link(x + w - forgot, y + 21.0f, "Forgot password?", kQuiet)) openUrl(place_.site + "/");
    y += 44.0f;
    if (button({x, y, w, 56.0f}, busy_ ? "Signing in..." : "Sign in", true, busy_) || (enter_ && !busy_)) signIn();
    y += 56.0f + 16.0f;
    if (!message_.empty()) alert({x, y, w, 44.0f}, message_);
    words(x, kH - 40.0f, "New to MU?", hex(kQuiet), 15.0f);
    if (link(x + measure("New to MU?", 15.0f) + 5.0f, kH - 40.0f, "Create account", kLabel)) openUrl(place_.site + "/");
}

void Launcher::drawHome() {
    // The wallpaper falls to dark under the logo and the button.
    canvas_.shade(px({0, kH * 0.45f, kW, kH * 0.45f}), hex(0x0D0B0A, 0.0f), hex(0x0D0B0A, 0.0f), hex(0x0D0B0A, 0.95f),
                  hex(0x0D0B0A, 0.95f));
    canvas_.rect(px({0, kH * 0.9f, kW, kH * 0.1f + 1.0f}), hex(0x0D0B0A, 0.95f));
#ifdef _WIN32
    const float right = kW - 100.0f, top = 0.0f;
#else
    const float right = kW - 30.0f, top = 4.0f;
#endif
    float logOutWide = 0;
    if (flat(right, top, "Log out", &logOutWide)) {
        logOut();
        return;
    }
    if (flat(right - logOutWide - 4.0f, top, "Edit account")) openUrl(place_.site + "/");

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
            if (button(main, "Play", true, false, 20.0f)) playNow();
            panel(px({textX, rowY + 24.0f, 8.0f, 8.0f}), 4.0f * s_, hex(0x7FD66A), hex(0x7FD66A));
            words(textX + 16.0f, rowY + 33.0f, "Up to date", hex(kQuiet), 15.0f);
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
    if (!manifest_.version.empty()) {
        const std::string version = "Client " + manifest_.version;
        words(kW - 40.0f - measure(version, 13.0f), kH - 40.0f, version, hex(kQuiet), 13.0f);
    }
}

void Launcher::draw() {
    widgetIndex_ = 0;
    wantCursor_ = nullptr;
    canvas_.clear();
    canvas_.image(wallpaper_, px({0, 0, kW, kH}));
    if (screen_ == Screen::SignIn) drawSignIn();
    else drawHome();
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
    init.type = bgfx::RendererType::Vulkan;
#else
    init.type = bgfx::RendererType::Metal;
#endif
    // The window on the swap chain, taken from init's own so its formats stay (gfx/window.cpp).
    chain_ = init.swapChain;
#ifdef _WIN32
    chain_.nwh = glfwGetWin32Window(window_);
#else
    chain_.nwh = glfwGetCocoaWindow(window_);
#endif
    chain_.width = uint32_t(fbW_);
    chain_.height = uint32_t(fbH_);
    init.swapChain = chain_;
    init.reset = BGFX_RESET_VSYNC;
    init.callback = &callback_;
    bgfx::renderFrame();
    if (!bgfx::init(init)) return false;
    if (!iface_.init("embedded")) return false;
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
    for (bgfx::TextureHandle t : {wordTex_, bodyTex_, wallpaper_.handle, logo_.handle}) {
        if (bgfx::isValid(t)) bgfx::destroy(t);
    }
    iface_.shutdown();
    bgfx::shutdown();
    if (window_) glfwDestroyWindow(window_);
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
    if (clicked_ && my_ < 40.0f * s_ && mx_ < (kW - 260.0f) * s_ && mx_ > 76.0f * s_) {
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
    if (screen_ == Screen::Home && enter_ && home_ == Home::Ready) playNow();
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
    if (!open()) {
        core::logError("launcher: the window did not open");
        close();
        return 1;
    }
    if (!options_.shot.empty()) {
        // A screen set up by hand, drawn a few times so its textures are in, then saved.
        const std::string& which = options_.shotScreen;
        manifest_.version = "354bb0d9";
        settings_.install = defaultInstall();
        if (which == "error") {
            fields_[0].text = "jaanis@example.com";
            message_ = "Wrong email or password.";
        } else if (which != "signin") {
            screen_ = Screen::Home;
            todoBytes_ = 1528000000ull;
            home_ = which == "install" ? Home::Install : which == "downloading" ? Home::Downloading : Home::Ready;
            progress_.total = todoBytes_;
            progress_.done = todoBytes_ * 38 / 100;
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
        check();
    } else {
        focus_ = settings_.email.empty() ? 0 : 1;
    }
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
