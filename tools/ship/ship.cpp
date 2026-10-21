// **Ship** -- a window over a release: what is live where, the commits players do not have yet,
// what they need (a server restart, a client patch, the launcher, the site, a protocol bump), the
// steps to ship them with a Run button each, their output as it comes, and the front page's Latest
// updates -- drafted from what went live, worded, published (the user, 2026-10-19: "all in one
// programm, which checks latest commits which is not shipped ... minicms for latest update on
// site", then "can we build app with some UI for this?", "make app in c++").
//
//   ./mu ship app                     build it and open it
//   out/build/mu_ship [--web URL]     a local web/server.py's lines instead of the box's
//   out/build/mu_ship --tab updates --shot out.png   a picture once the status is in, and close
//   out/build/mu_ship --log out/logs/ship/X.log      a kept run's output, read by lane
//
// What it knows it asks of `./mu ship ... --json` (tools/mu/ship.py), which reads the manifests,
// the boxes and git: the rules are there once, and the terminal and this window say the same. Each
// step is the command the report names, run under zsh in MU2_BGFX with its output here; a LIVE one
// (a server restart, a release players get) is asked about first. ImGui on bgfx, as botwatch.
//
// The steps are two phases (the user, 2026-10-21: "designed for minimal downtime"): `mu ship
// prepare` builds and stages everything while players play, the boxes side by side; `mu ship
// switch` makes it live at one moment, each box's players told 60 s first. Their lanes' lines come
// marked [North Europe], [windows] ... in the one console. `./deploy.sh` in MU2_BGFX opens this.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <GLFW/glfw3.h>
#include <bgfx/bgfx.h>

#include "core/json.h"
#include "gfx/gui.h"
#include "gfx/window.h"
#include "imgui.h"

extern char** environ;

namespace {

using mu::core::Json;
using Clock = std::chrono::steady_clock;

volatile std::sig_atomic_t g_quit = 0;
void onSignal(int) { g_quit = 1; }

const ImVec4 kDim(0.55f, 0.57f, 0.61f, 1.0f);
const ImVec4 kGold(0.88f, 0.69f, 0.29f, 1.0f);
const ImVec4 kRed(0.88f, 0.42f, 0.37f, 1.0f);
const ImVec4 kGreen(0.42f, 0.77f, 0.54f, 1.0f);
const ImVec4 kBlue(0.45f, 0.62f, 0.95f, 1.0f);

// ---- a child process ------------------------------------------------------------------------------

// One command under zsh in the root, its stdout and stderr gathered as they come by a thread of its
// own. Its own process group, so Stop ends what it started too (dist's cmake, release's rsync).
class Job {
public:
    Job(const std::string& root, const std::string& command) : command_(command) {
        int pipeFds[2];
        if (pipe(pipeFds) != 0) {
            finish(-1, "could not open a pipe\n");
            return;
        }
        posix_spawn_file_actions_t files;
        posix_spawn_file_actions_init(&files);
        posix_spawn_file_actions_addopen(&files, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
        posix_spawn_file_actions_adddup2(&files, pipeFds[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&files, pipeFds[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&files, pipeFds[0]);
        posix_spawnattr_t attr;
        posix_spawnattr_init(&attr);
        posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
        posix_spawnattr_setpgroup(&attr, 0);
        const std::string script = "cd '" + root + "' && " + command;
        const char* argv[] = {"/bin/zsh", "-c", script.c_str(), nullptr};
        const int failed = posix_spawn(&pid_, argv[0], &files, &attr, const_cast<char**>(argv), environ);
        posix_spawn_file_actions_destroy(&files);
        posix_spawnattr_destroy(&attr);
        close(pipeFds[1]);
        if (failed != 0) {
            close(pipeFds[0]);
            finish(-1, "could not start zsh\n");
            return;
        }
        started_ = Clock::now();
        reader_ = std::thread([this, fd = pipeFds[0]] {
            char chunk[4096];
            for (;;) {
                const ssize_t n = read(fd, chunk, sizeof chunk);
                if (n <= 0) break;
                {
                    std::lock_guard<std::mutex> hold(lock_);
                    out_.append(chunk, size_t(n));
                }
                glfwPostEmptyEvent();
            }
            close(fd);
            int status = 0;
            waitpid(pid_, &status, 0);
            finish(WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0), "");
        });
    }
    ~Job() {
        if (!done_) stop();
        if (reader_.joinable()) reader_.join();
    }
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;

    void stop() {
        if (pid_ > 0 && !done_) kill(-pid_, SIGTERM);
    }
    bool done() const { return done_; }
    bool ok() const { return done_ && exit_ == 0; }
    int exitCode() const { return exit_; }
    const std::string& command() const { return command_; }
    double seconds() const {
        return std::chrono::duration<double>((done_ ? ended_ : Clock::now()) - started_).count();
    }
    std::string output() const {
        std::lock_guard<std::mutex> hold(lock_);
        return out_;
    }
    // `mu ... --json` ends with one JSON line: {ok, command, data}.
    Json result() const {
        const std::string all = output();
        size_t end = all.find_last_not_of("\n ");
        if (end == std::string::npos) return {};
        const size_t start = all.rfind('\n', end);
        const std::string line = all.substr(start == std::string::npos ? 0 : start + 1, end + 1 - (start == std::string::npos ? 0 : start + 1));
        if (line.empty() || line[0] != '{') return {};
        return mu::core::parseJson(line.data(), line.size(), "mu --json");
    }

private:
    void finish(int code, const char* said) {
        {
            std::lock_guard<std::mutex> hold(lock_);
            out_ += said;
        }
        exit_ = code;
        ended_ = Clock::now();
        done_ = true;
        glfwPostEmptyEvent();
    }

    std::string command_;
    pid_t pid_ = -1;
    std::thread reader_;
    mutable std::mutex lock_;
    std::string out_;
    std::atomic<bool> done_{false};
    std::atomic<int> exit_{0};
    Clock::time_point started_ = Clock::now(), ended_ = Clock::now();
};

std::string quoted(const std::string& s) {
    std::string out = "'";
    for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}

std::string jsonString(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') out += '\\', out += char(c);
        else if (c == '\n') out += "\\n";
        else if (c < 0x20) {
            char hex[8];
            std::snprintf(hex, sizeof hex, "\\u%04x", c);
            out += hex;
        } else out += char(c);
    }
    return out + "\"";
}

std::string today() {
    const std::time_t now = std::time(nullptr);
    char day[16];
    std::strftime(day, sizeof day, "%Y-%m-%d", std::localtime(&now));
    return day;
}

std::string longDay(const std::string& day) {
    static const char* months[] = {"January", "February", "March", "April", "May", "June", "July",
                                   "August", "September", "October", "November", "December"};
    int y = 0, m = 0, d = 0;
    if (std::sscanf(day.c_str(), "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12) return day;
    char text[48];
    std::snprintf(text, sizeof text, "%d %s %d", d, months[m - 1], y);
    return text;
}

// ---- what the window shows ---------------------------------------------------------------------

// What a switch's told servers wait before they restart (server/src/host.h kRestartWarn).
constexpr double kWarnSeconds = 60.0;

// One lane of a step's output: what `mu ship prepare` and `switch` say of one place, "[windows] ...".
struct Lane {
    std::string label;
    std::vector<std::string> lines;
    enum class State { Waiting, Running, Done, Failed } state = State::Waiting;
    Clock::time_point started{}, ended{}, toldAt{};
    bool restarted = false;  // a told server back on its new build
};

struct Step {
    std::string command, note;
    bool live = false;
    enum class State { Waiting, Running, Done, Failed, Skipped } state = State::Waiting;
    double seconds = 0.0;
};

// An update line, the site's (id > 0) or a draft not yet sent (id 0).
struct Line {
    int id = 0;
    char day[16] = {};
    int kind = 0;  // 0 new, 1 fixed
    char text[512] = {};
    bool published = false;
    bool include = true;  // a draft: sent with the others
    std::vector<std::string> commits;
    bool armed = false;   // delete asked once
};

const char* kKinds[] = {"New", "Fixed"};

class App {
public:
    App(std::string root, std::string web) : root_(std::move(root)), web_(std::move(web)) {
        std::snprintf(draftDay_, sizeof draftDay_, "%s", today().c_str());
    }

    void refresh() {
        if (status_ && !status_->done()) return;
        status_ = std::make_unique<Job>(root_, "./mu ship --json");
    }
    void loadLines() {
        if (lines_ && !lines_->done()) return;
        lines_ = std::make_unique<Job>(root_, "./mu ship updates --json" + webFlag());
    }
    bool statusIn() const { return !plan_.isNull(); }
    // --draft-from SHA: as if typed into "from" and Draft pressed, once the site's lines are in.
    void draftFrom(const std::string& sha) { autoDraft_ = sha; }
    void openTab(const std::string& t) { wantTab_ = t; }
    // --log FILE: a kept run's output (out/logs/ship/) read into the console and its lanes.
    void showLog(const std::string& path) {
        logJob_ = std::make_unique<Job>(root_, "cat " + quoted(path));
        console_ = logJob_.get();
        lastLog_ = path;
    }

    // Each frame: whatever finished is taken in; the next of a run of steps is started.
    void poll() {
        if (!runAll_ && running_ < 0) shipAll_ = false;
        if (status_ && status_->done() && !statusTaken_) {
            statusTaken_ = true;
            const Json said = status_->result();
            if (said["data"].type == Json::Type::Object) {
                plan_ = said["data"];
                checkedAt_ = Clock::now();
                steps_.clear();
                for (size_t i = 0; i < plan_["steps"].size(); ++i) {
                    Step s;
                    s.command = plan_["steps"].at(i).stringOr("");
                    s.note = plan_["step_notes"].at(i).stringOr("");
                    s.live = s.note.rfind("LIVE", 0) == 0;
                    steps_.push_back(s);
                }
                statusError_.clear();
                if (running_ < 0 && !runAll_) failure_.clear();
            } else {
                statusError_ = lastLines(status_->output(), 6);
            }
        }
        if (status_ && !status_->done()) statusTaken_ = false;

        if (lines_ && lines_->done() && !linesTaken_) {
            linesTaken_ = true;
            const Json said = lines_->result();
            if (said["data"].type == Json::Type::Object) {
                site_.clear();
                const Json& entries = said["data"]["entries"];
                for (size_t i = 0; i < entries.size(); ++i) site_.push_back(lineOf(entries.at(i)));
                // "from" follows the site only while nobody has typed another commit into it.
                const std::string was = notedThrough_;
                notedThrough_ = said["data"]["noted_through"].stringOr("");
                if (!since_[0] || was == since_) std::snprintf(since_, sizeof since_, "%s", notedThrough_.c_str());
                siteError_.clear();
            } else {
                siteError_ = lastLines(lines_->output(), 6);
            }
        }
        if (lines_ && !lines_->done()) linesTaken_ = false;
        if (!autoDraft_.empty() && lines_ && lines_->done() && linesTaken_) {
            std::snprintf(since_, sizeof since_, "%s", autoDraft_.c_str());
            autoDraft_.clear();
            draft();
        }

        if (drafting_ && drafting_->done() && !draftTaken_) {
            draftTaken_ = true;
            const Json said = drafting_->result();
            if (said["ok"].boolOr(false)) {
                drafts_.clear();
                const Json& got = said["data"]["drafts"];
                for (size_t i = 0; i < got.size(); ++i) drafts_.push_back(lineOf(got.at(i)));
                draftsThrough_ = said["data"]["noted_through"].stringOr("");
                draftError_.clear();
            } else {
                draftError_ = lastLines(drafting_->output(), 6);
                // The site keeps no lines yet: say what to press, and read it again so the button shows.
                if (draftError_.find("noted_through") != std::string::npos)
                    draftError_ = "The site keeps no lines yet: press \"Move the page's lines in\" below once, then Draft.";
                loadLines();
            }
        }

        // The site's queue: one send at a time, the lines read again after each.
        if (sending_ && sending_->done()) {
            if (!sending_->ok()) siteError_ = lastLines(sending_->output(), 6);
            sending_.reset();
            loadLines();
        }
        if (!sending_ && !outbox_.empty()) {
            const std::string body = outbox_.front();
            outbox_.erase(outbox_.begin());
            char path[] = "/tmp/mu-ship-send-XXXXXX";
            const int fd = mkstemp(path);
            if (fd >= 0) {
                (void)!write(fd, body.data(), body.size());
                close(fd);
                sending_ = std::make_unique<Job>(root_, "./mu ship send " + quoted(path) + " --json" + webFlag() +
                                                            "; rm -f " + quoted(path));
            }
        }

        parseLanes();
        // The step running, and the next of a Run all.
        if (running_ >= 0 && step_ && step_->done()) {
            Step& s = steps_[size_t(running_)];
            s.seconds = step_->seconds();
            s.state = step_->ok() ? Step::State::Done : Step::State::Failed;
            keepLog(running_, *step_);
            if (s.command.rfind("web/deploy.sh", 0) == 0) loadLines();  // a new site: its lines read again
            const bool wasLive = s.live;
            const int was = running_;
            running_ = -1;
            if (s.state == Step::State::Failed) runAll_ = false;
            if (runAll_) {
                const int next = nextWaiting(was + 1);
                if (next < 0) runAll_ = false, needsRefresh_ = true;
                else ask(next);
            }
            if ((wasLive || s.command.rfind("./mu ship prepare", 0) == 0) && !runAll_) needsRefresh_ = true;
        }
        if (needsRefresh_ && running_ < 0 && !runAll_) {
            needsRefresh_ = false;
            refresh();
        }
    }

    void draw(int width, int height) {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(float(width), float(height)));
        ImGui::Begin("ship", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
        header();
        ImGui::Spacing();
        if (ImGui::BeginTabBar("tabs")) {
            const auto flagsFor = [&](const char* name) {
                if (wantTab_ == name) {
                    wantTab_.clear();
                    return ImGuiTabItemFlags_SetSelected;
                }
                return ImGuiTabItemFlags_None;
            };
            if (ImGui::BeginTabItem("Release", nullptr, flagsFor("release"))) {
                releaseTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Latest updates", nullptr, flagsFor("updates"))) {
                if (!lines_) loadLines();
                updatesTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        confirmPopup();
        ImGui::End();
    }

    bool busy() const {
        return (status_ && !status_->done()) || (step_ && !step_->done()) || (drafting_ && !drafting_->done()) ||
               sending_ || (lines_ && !lines_->done());
    }

private:
    std::string webFlag() const { return web_.empty() ? "" : " --web " + quoted(web_); }

    static std::string lastLines(const std::string& text, int count) {
        size_t at = text.size();
        while (at > 0 && (text[at - 1] == '\n' || text[at - 1] == ' ')) --at;
        size_t from = at;
        for (int n = 0; n < count && from > 0; ++n) {
            const size_t nl = text.rfind('\n', from - 1);
            from = nl == std::string::npos ? 0 : nl;
            if (nl == std::string::npos) break;
        }
        return text.substr(from, at - from);
    }

    static Line lineOf(const Json& j) {
        Line l;
        l.id = int(j["id"].numberOr(0));
        std::snprintf(l.day, sizeof l.day, "%s", j["day"].stringOr("").c_str());
        l.kind = j["kind"].stringOr("new") == "fixed" ? 1 : 0;
        std::snprintf(l.text, sizeof l.text, "%s", j["text"].stringOr("").c_str());
        l.published = j["state"].stringOr("draft") == "published";
        for (size_t i = 0; i < j["commits"].size(); ++i) l.commits.push_back(j["commits"].at(i).stringOr(""));
        return l;
    }

    static std::string lineJson(const Line& l, bool withId, bool published) {
        std::string out = "{";
        if (withId) out += "\"id\":" + std::to_string(l.id) + ",";
        out += "\"day\":" + jsonString(l.day) + ",\"kind\":" + jsonString(l.kind ? "fixed" : "new") +
               ",\"text\":" + jsonString(l.text) + ",\"state\":" + jsonString(published ? "published" : "draft") +
               ",\"commits\":[";
        for (size_t i = 0; i < l.commits.size(); ++i) out += (i ? "," : "") + jsonString(l.commits[i]);
        return out + "]}";
    }

    void saveLine(const Line& l) { outbox_.push_back("{\"entries\":[" + lineJson(l, true, l.published) + "]}"); }

    int nextWaiting(int from) const {
        for (int i = from; i < int(steps_.size()); ++i) {
            if (steps_[size_t(i)].state == Step::State::Waiting || steps_[size_t(i)].state == Step::State::Failed) return i;
        }
        return -1;
    }

    // A LIVE step is asked about first; any other runs at once.
    void ask(int i) {
        if (steps_[size_t(i)].live && !shipAll_) {
            confirm_ = i;  // the popup opens itself on the next frame (confirmPopup)
        } else {
            start(i);
        }
    }

    void start(int i) {
        Step& s = steps_[size_t(i)];
        if (s.command == "./mu ship notes") {  // the window's own: the Latest updates tab drafts them
            s.state = Step::State::Done;
            openTab("updates");
            draft();
            if (runAll_) {
                const int next = nextWaiting(i + 1);
                if (next < 0) runAll_ = false;
                else ask(next);
            }
            return;
        }
        const bool now = s.command.rfind("./mu ship switch", 0) == 0 && !warnPlayers_;
        step_ = std::make_unique<Job>(root_, s.command + (now ? " --now" : ""));
        console_ = step_.get();
        s.state = Step::State::Running;
        running_ = i;
    }

    // Every step's output kept in out/logs/ship/, so a failure can be read after the window shut;
    // a failed one's last lines stand by the verdict too.
    void keepLog(int i, const Job& job) {
        const std::time_t now = std::time(nullptr);
        char stamp[32];
        std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&now));
        const std::string folder = root_ + "/out/logs/ship";
        std::system(("mkdir -p " + quoted(folder)).c_str());
        lastLog_ = folder + "/" + stamp + "-step" + std::to_string(i + 1) + ".log";
        if (FILE* f = std::fopen(lastLog_.c_str(), "w")) {
            const std::string text = "$ " + job.command() + "\n" + job.output() + "\nexit " + std::to_string(job.exitCode()) + "\n";
            std::fwrite(text.data(), 1, text.size(), f);
            std::fclose(f);
        }
        failure_ = job.ok() ? "" : "Step " + std::to_string(i + 1) + " failed (" + job.command() + "):\n" +
                                       lastLines(job.output(), 4) + "\nWhole output: " + lastLog_;
    }

    void draft() {
        if (drafting_ && !drafting_->done()) return;
        std::string cmd = "./mu ship notes --dry --json --day " + quoted(draftDay_) + webFlag();
        if (plain_) cmd += " --plain";
        if (since_[0] && notedThrough_ != since_) cmd += " --since " + quoted(since_);
        drafting_ = std::make_unique<Job>(root_, cmd);
        draftTaken_ = false;
    }

    // ---- the header ----

    void header() {
        const std::string head = plan_["head"].stringOr("");
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.35f);
        ImGui::TextColored(kGold, "Ship");
        ImGui::PopFont();
        ImGui::SameLine();
        if (!head.empty()) {
            ImGui::TextDisabled("HEAD");
            ImGui::SameLine();
            ImGui::Text("%s", head.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("protocol %d", int(plan_["protocol"].numberOr(0)));
            ImGui::SameLine();
            std::string subject = plan_["subject"].stringOr("");
            if (subject.rfind("MU2_BGFX: ", 0) == 0) subject = subject.substr(10);
            if (subject.size() > 110) subject = subject.substr(0, 107) + "...";
            ImGui::TextDisabled("%s", subject.c_str());
        }
        const float right = ImGui::GetWindowWidth() - 250.0f;
        ImGui::SameLine(std::max(right, ImGui::GetCursorPosX() + 10.0f));
        const bool checking = status_ && !status_->done();
        if (checking) {
            ImGui::TextColored(kDim, "checking the boxes... %.0f s", status_->seconds());
        } else {
            const int ago = statusIn() ? int(std::chrono::duration<double>(Clock::now() - checkedAt_).count()) : -1;
            if (ago >= 0) ImGui::TextDisabled(ago < 60 ? "checked %d s ago" : "checked %d min ago", ago < 60 ? ago : ago / 60);
            ImGui::SameLine();
            if (ImGui::Button("Refresh")) refresh();
        }
    }

    // ---- the release ----
    //
    // (the user, 2026-10-21: "improve UX/UI for deploy app") Three phases across the top, each a
    // card with its one button: Prepare (nothing players see), Go live (the moment), Tell players
    // (the front page's lines). Under them every place a release lands is a card of its own, which
    // during a run shows its lane of the step's output; the commits not yet shipped beside them;
    // the output below, all of it or one lane's.

    static ImU32 u32(const ImVec4& c, float alpha = 1.0f) { return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * alpha)); }

    // A small rounded label in a tone.
    static void chip(const char* text, const ImVec4& tone) {
        const ImVec2 size = ImGui::CalcTextSize(text);
        const ImVec2 pad(9, 3);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const ImVec2 end(at.x + size.x + pad.x * 2, at.y + size.y + pad.y * 2);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(at, end, u32(tone, 0.16f), 10.0f);
        draw->AddRect(at, end, u32(tone, 0.5f), 10.0f);
        draw->AddText(ImVec2(at.x + pad.x, at.y + pad.y), u32(tone), text);
        ImGui::Dummy(ImVec2(end.x - at.x, end.y - at.y));
    }

    // A coloured dot on the text's line.
    static void dot(const ImVec4& tone) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetTextLineHeight();
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(at.x + 5, at.y + h * 0.5f), 4.5f, u32(tone));
        ImGui::Dummy(ImVec2(12, h));
        ImGui::SameLine();
    }

    int stepNamed(const char* prefix) const {
        for (size_t i = 0; i < steps_.size(); ++i) {
            if (steps_[i].command.rfind(prefix, 0) == 0) return int(i);
        }
        return -1;
    }
    bool prepared() const {
        const int prep = stepNamed("./mu ship prepare");
        return !plan_["prepared"].isNull() || (prep >= 0 && steps_[size_t(prep)].state == Step::State::Done);
    }
    // The servers a switch restarts: behind, staged or stopped.
    std::vector<const Json*> switching() const {
        std::vector<const Json*> out;
        const Json& rows = plan_["rows"];
        for (size_t i = 0; i < rows.size(); ++i) {
            const Json& r = rows.at(i);
            if (r["kind"].stringOr("") != "server") continue;
            if (r["behind"].numberOr(1) != 0 || r["how"].stringOr("running") != "running") out.push_back(&r);
        }
        return out;
    }

    void releaseTab() {
        if (!statusError_.empty()) ImGui::TextColored(kRed, "mu ship failed:\n%s", statusError_.c_str());
        if (!statusIn()) {
            ImGui::TextDisabled("Reading the manifests, the servers and the site...");
            return;
        }
        banner();
        ImGui::Spacing();
        phases();
        countdown();
        ImGui::Spacing();
        const float consoleHeight = console_ ? std::max(170.0f, ImGui::GetContentRegionAvail().y * 0.32f) : 90.0f;
        const float top = ImGui::GetContentRegionAvail().y - consoleHeight - 8.0f;
        if (ImGui::BeginTable("cols", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImVec2(0, top))) {
            ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthStretch, 0.58f);
            ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthStretch, 0.42f);
            ImGui::TableNextColumn();
            ImGui::BeginChild("left", ImVec2(0, top - 4));
            targets();
            ImGui::EndChild();
            ImGui::TableNextColumn();
            ImGui::BeginChild("right", ImVec2(0, top - 4));
            unshipped();
            ImGui::EndChild();
            ImGui::EndTable();
        }
        console(consoleHeight);
    }

    // What is waiting, in a line and a row of chips; Ship updates runs every phase.
    void banner() {
        const bool bump = plan_["bump"].boolOr(false);
        const Json& todo = plan_["todo"];
        const size_t boxes = todo["boxes"].size(), clients = todo["clients"].size();
        const bool launcher = todo["launcher"].boolOr(false), web = todo["web"].boolOr(false);
        const bool nothing = !boxes && !clients && !launcher && !web;
        const ImVec4 tone = bump ? kRed : nothing ? kGreen : kGold;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(tone.x * 0.14f, tone.y * 0.14f, tone.z * 0.14f, 1.0f));
        ImGui::BeginChild("banner", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        if (!steps_.empty() && !nothing) {
            const bool idle = running_ < 0 && !runAll_;
            ImGui::BeginDisabled(!idle || nextWaiting(0) < 0);
            ImGui::PushStyleColor(ImGuiCol_Button, bump ? ImVec4(0.62f, 0.22f, 0.18f, 1) : ImVec4(0.62f, 0.46f, 0.15f, 1));
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.2f);
            if (ImGui::Button(shipAll_ ? "Shipping..." : "Ship updates", ImVec2(170, 46))) askShip_ = true;
            ImGui::PopFont();
            ImGui::PopStyleColor();
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Prepare, then go live, then draft the notes: asked once.");
            ImGui::SameLine(0, 16);
        }
        ImGui::BeginGroup();
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.2f);
        ImGui::TextColored(tone, "%s", nothing ? "Everything is live" : bump ? "A protocol change: every server and client go together"
                                                                              : "Updates waiting to ship");
        ImGui::PopFont();
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(620.0f);
            ImGui::TextUnformatted(plan_["verdict"].stringOr("").c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        if (!nothing) {
            char text[160];
            if (bump) {
                std::snprintf(text, sizeof text, "protocol %d", int(plan_["protocol"].numberOr(0)));
                chip(text, kRed);
                ImGui::SameLine();
            }
            if (boxes) {
                std::snprintf(text, sizeof text, "%zu server%s restart", boxes, boxes == 1 ? "" : "s");
                chip(text, kRed);
                ImGui::SameLine();
            }
            if (clients) {
                std::string names;
                for (size_t i = 0; i < clients; ++i) names += (i ? ", " : "") + todo["clients"].at(i).stringOr("");
                std::snprintf(text, sizeof text, "client patch: %s", names.c_str());
                chip(text, kBlue);
                ImGui::SameLine();
            }
            if (launcher) {
                chip("launcher", kGold);
                ImGui::SameLine();
            }
            if (web) {
                chip("site", kGreen);
                ImGui::SameLine();
            }
            ImGui::NewLine();
        }
        const Json& errors = plan_["errors"];
        for (size_t i = 0; i < errors.size(); ++i) ImGui::TextColored(kRed, "could not read %s", errors.at(i).stringOr("").c_str());
        const Json& dirty = plan_["dirty"];
        if (dirty.size()) {
            ImGui::TextDisabled("%zu uncommitted files in shipped paths (another session's?) -- only %s ships",
                                dirty.size(), plan_["head"].stringOr("").c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                for (size_t i = 0; i < dirty.size(); ++i) ImGui::TextUnformatted(dirty.at(i).stringOr("").c_str());
                ImGui::EndTooltip();
            }
        }
        if (!failure_.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(kRed, "%s", failure_.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndGroup();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // ---- the phases ----

    struct Phase {
        const char* id;
        int number;
        const char* title;
        const char* about;
        int step;            // its step, or -1
        bool done;           // done before this window ran it (a prepare on disk)
        std::string doneText;
        const char* button;
        ImVec4 tone;
        bool enabled;
        std::string waiting;  // said when there is nothing to press yet
    };

    void phases() {
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float width = (ImGui::GetContentRegionAvail().x - gap * 2) / 3.0f;
        const int prep = stepNamed("./mu ship prepare"), sw = stepNamed("./mu ship switch"), notes = stepNamed("./mu ship notes");
        const bool ready = prepared();
        const bool nothing = steps_.empty();

        std::string at = plan_["prepared"]["at"].stringOr("");
        Phase p{"prepare", 1, "Prepare", "Build and stage on every box and for every client, side by side. Players see nothing.",
                prep, ready, at.size() >= 16 ? "Ready since " + at.substr(11, 5) : "Ready", "Prepare", kGold, prep >= 0,
                nothing ? "Nothing to prepare" : ""};
        if (phaseCard(p, width) && prep >= 0) ask(prep);
        ImGui::SameLine();

        const size_t restarting = switching().size();
        char about[200];
        std::snprintf(about, sizeof about, "%s%s at one moment, then each read back where it lives.",
                      restarting ? (warnPlayers_ ? "Players told 60 s, servers restart, " : "Servers restart at once, ") : "",
                      "updates go live");
        Phase g{"golive", 2, "Go live", about, sw, false, "Live", "Go live...", kRed, sw >= 0 && ready,
                nothing ? "Nothing to ship" : (!ready ? "After Prepare" : "")};
        if (phaseCard(g, width) && sw >= 0) ask(sw);
        ImGui::SameLine();

        Phase t{"tell", 3, "Tell players", "Draft the front page's Latest updates from what went live; an admin publishes them.",
                notes, false, "Drafted", "Draft notes", kBlue, notes >= 0, nothing ? "Nothing new to tell" : ""};
        if (phaseCard(t, width)) {
            if (notes >= 0) ask(notes);
        }
    }

    // One phase: its number, title and what it does; its state and its button at the foot.
    bool phaseCard(const Phase& p, float width) {
        const Step* s = p.step >= 0 ? &steps_[size_t(p.step)] : nullptr;
        const bool running = s && s->state == Step::State::Running;
        const bool failed = s && s->state == Step::State::Failed;
        const bool done = (s && s->state == Step::State::Done) || (!s && p.done);
        const ImVec4 tone = failed ? kRed : running ? kGold : done ? kGreen : kDim;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.09f, 0.10f, 0.125f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, running ? ImVec4(kGold.x, kGold.y, kGold.z, 0.6f) : ImVec4(1, 1, 1, 0.08f));
        ImGui::BeginChild(p.id, ImVec2(width, 118), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar);
        // The number in a ring, filled once done.
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float r = 13.0f;
        const ImVec2 centre(at.x + r, at.y + r);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (done || running || failed) draw->AddCircleFilled(centre, r, u32(tone, done ? 0.9f : 0.25f));
        draw->AddCircle(centre, r, u32(tone), 0, 2.0f);
        char number[4];
        std::snprintf(number, sizeof number, "%d", p.number);
        const ImVec2 size = ImGui::CalcTextSize(number);
        draw->AddText(ImVec2(centre.x - size.x * 0.5f, centre.y - size.y * 0.5f),
                      done ? u32(ImVec4(0.07f, 0.08f, 0.09f, 1)) : u32(tone), number);
        ImGui::Dummy(ImVec2(r * 2, r * 2));
        ImGui::SameLine(0, 10);
        ImGui::BeginGroup();
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.2f);
        ImGui::TextUnformatted(p.title);
        ImGui::PopFont();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 70);
        ImGui::TextColored(kDim, "%s", p.about);
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();

        // The foot: the state on the left, the button on the right.
        const float footY = 118 - ImGui::GetStyle().WindowPadding.y - ImGui::GetFrameHeight() - 2;
        ImGui::SetCursorPosY(footY);
        if (running) ImGui::TextColored(kGold, "running %s", clock(step_ ? step_->seconds() : 0.0).c_str());
        else if (failed) ImGui::TextColored(kRed, "failed -- the output below says why");
        else if (s && s->state == Step::State::Done) ImGui::TextColored(kGreen, "%s in %s", p.doneText.c_str(), clock(s->seconds).c_str());
        else if (done) ImGui::TextColored(kGreen, "%s", p.doneText.c_str());
        else if (s && s->state == Step::State::Skipped) ImGui::TextDisabled("skipped");
        else if (!p.waiting.empty()) ImGui::TextDisabled("%s", p.waiting.c_str());
        bool pressed = false;
        const float buttonW = 120;
        ImGui::SameLine(width - buttonW - ImGui::GetStyle().WindowPadding.x);
        ImGui::SetCursorPosY(footY - 3);
        if (running) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.16f, 0.14f, 1));
            if (ImGui::Button("Stop", ImVec2(buttonW, 0)) && step_) step_->stop();
            ImGui::PopStyleColor();
        } else {
            const bool idle = running_ < 0 && !runAll_;
            ImGui::BeginDisabled(!p.enabled || !idle);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(p.tone.x * 0.62f, p.tone.y * 0.62f, p.tone.z * 0.62f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(p.tone.x * 0.8f, p.tone.y * 0.8f, p.tone.z * 0.8f, 1));
            pressed = ImGui::Button(done ? "Again" : p.button, ImVec2(buttonW, 0));
            ImGui::PopStyleColor(2);
            ImGui::EndDisabled();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        return pressed;
    }

    static std::string clock(double seconds) {
        char text[24];
        const int s = int(seconds + 0.5);
        if (s < 60) std::snprintf(text, sizeof text, "%d s", s);
        else std::snprintf(text, sizeof text, "%d:%02d", s / 60, s % 60);
        return text;
    }

    // While the servers tell their players: how long until they restart, as a bar.
    void countdown() {
        std::string told;
        double longest = -1.0;
        int back = 0, telling = 0;
        for (const Lane& lane : lanes_) {
            if (lane.toldAt == Clock::time_point{}) continue;
            if (lane.restarted) {
                ++back;
                continue;
            }
            ++telling;
            told += (told.empty() ? "" : ", ") + lane.label;
            longest = std::max(longest, kWarnSeconds - std::chrono::duration<double>(Clock::now() - lane.toldAt).count());
        }
        if (!telling && !(back && console_ && !console_->done())) return;
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.20f, 0.09f, 0.08f, 1.0f));
        ImGui::BeginChild("countdown", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        if (telling) {
            const double left = std::max(0.0, longest);
            ImGui::TextColored(kRed, "Players on %s are being told.", told.c_str());
            ImGui::SameLine();
            ImGui::Text(left > 0.5 ? "The servers restart in %.0f s." : "The servers are restarting...", left);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.78f, 0.36f, 0.30f, 1));
            ImGui::ProgressBar(float(1.0 - left / kWarnSeconds), ImVec2(-1, 6), "");
            ImGui::PopStyleColor();
        } else {
            ImGui::TextColored(kGreen, "The servers are back; the clients' updates are turning.");
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // ---- where it lands ----

    void targets() {
        targetGroup("Servers", "server");
        targetGroup("Clients", "client");
        targetGroup("Launcher and site", "launcher", "site");
    }

    void targetGroup(const char* title, const char* kind, const char* also = "") {
        const Json& rows = plan_["rows"];
        std::vector<size_t> mine;
        for (size_t i = 0; i < rows.size(); ++i) {
            const std::string k = rows.at(i)["kind"].stringOr("");
            if (k == kind || k == also) mine.push_back(i);
        }
        if (mine.empty()) return;
        ImGui::TextColored(kGold, "%s", title);
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float width = std::floor((ImGui::GetContentRegionAvail().x - gap * 3) / 4.0f);
        for (size_t n = 0; n < mine.size(); ++n) {
            if (n % 4) ImGui::SameLine();
            targetCard(rows.at(mine[n]), width, int(mine[n]));
        }
        ImGui::Spacing();
    }

    // The lane of the running step's output that lands on this row: a server by its region, a
    // client by its platform, the launcher's and the site's by name.
    const Lane* laneOf(const Json& r) const {
        const std::string kind = r["kind"].stringOr("");
        const std::string label = kind == "launcher" ? "launcher" : kind == "site" ? "site" : r["name"].stringOr("");
        for (const Lane& lane : lanes_) {
            if (lane.label == label) return &lane;
        }
        return nullptr;
    }

    void targetCard(const Json& r, float width, int id) {
        const Lane* lane = laneOf(r);
        const std::string how = r["how"].stringOr("running");
        const Json& behind = r["behind"];
        const bool stopped = how == "STOPPED";
        const bool staged = r["staged"].boolOr(false);
        const bool jobOver = !console_ || console_->done();
        // The lane says what is happening now; without one, the row says how it stands.
        ImVec4 tone;
        std::string badge;
        if (lane && lane->state == Lane::State::Failed) tone = kRed, badge = "failed";
        else if (lane && lane->state == Lane::State::Running && jobOver) tone = kRed, badge = "stopped";
        else if (lane && lane->state == Lane::State::Running && lane->toldAt != Clock::time_point{} && !lane->restarted)
            tone = kRed, badge = "telling players";
        else if (lane && lane->state == Lane::State::Running) tone = kGold, badge = "working";
        else if (lane && lane->state == Lane::State::Waiting) tone = kGold, badge = "waiting";
        else if (lane && lane->state == Lane::State::Done) tone = kGreen, badge = "done";
        else if (stopped) tone = kRed, badge = "STOPPED";
        else if (behind.type != Json::Type::Number) tone = kRed, badge = "off HEAD";
        else if (staged) tone = kBlue, badge = "staged";
        else if (behind.number == 0) tone = kGreen, badge = "current";
        else tone = kGold, badge = std::to_string(int(behind.number)) + " behind";

        const bool chosen = lane && laneFilter_ == lane->label;
        ImGui::PushID(id);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, chosen ? ImVec4(0.14f, 0.15f, 0.19f, 1) : ImVec4(0.09f, 0.10f, 0.125f, 1));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(tone.x, tone.y, tone.z, chosen ? 0.8f : 0.22f));
        ImGui::BeginChild("card", ImVec2(width, 78), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar);
        dot(tone);
        const std::string kind = r["kind"].stringOr("");
        ImGui::TextUnformatted((kind == "launcher" ? "launcher " + r["name"].stringOr("") : r["name"].stringOr("")).c_str());
        const float badgeW = ImGui::CalcTextSize(badge.c_str()).x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 6, width - badgeW - ImGui::GetStyle().WindowPadding.x));
        ImGui::TextColored(tone, "%s", badge.c_str());

        const int headProtocol = int(plan_["protocol"].numberOr(0));
        ImGui::TextDisabled("%s", r["live"].stringOr("?").c_str());
        if (r["protocol"].type == Json::Type::Number) {
            ImGui::SameLine();
            const int p = int(r["protocol"].number);
            ImGui::TextColored(p == headProtocol ? kDim : kRed, "protocol %d", p);
        }
        // The third line: the lane's last word, or what a switch would do here.
        if (lane) {
            const double secs = std::chrono::duration<double>(
                (lane->ended != Clock::time_point{} ? lane->ended : Clock::now()) - lane->started).count();
            const std::string last = lane->lines.empty() ? "" : lane->lines.back();
            if (lane->state == Lane::State::Running && !jobOver) ImGui::TextColored(kGold, "%s  %s", clock(secs).c_str(), last.c_str());
            else ImGui::TextColored(lane->state == Lane::State::Failed ? kRed : kDim, "%s", last.c_str());
        } else if (r["kind"].stringOr("") == "server") {
            if (r["warns"].boolOr(false)) ImGui::TextColored(kDim, "warns players 60 s before a restart");
            else ImGui::TextColored(kGold, "no restart warning yet");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Its running server is older than the 60 s warning: the next deploy restarts it at once.");
        }
        if (lane && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            laneFilter_ = chosen ? "" : lane->label;
        if (lane && ImGui::IsWindowHovered()) ImGui::SetTooltip(chosen ? "Show all output" : "Show only this lane's output");
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopID();
    }

    void unshipped() {
        const Json& list = plan_["unshipped"];
        ImGui::TextColored(kGold, "Not yet shipped");
        ImGui::SameLine();
        ImGui::TextDisabled("%zu commits, newest first", list.size());
        if (!list.size()) {
            ImGui::TextDisabled("Every commit is everywhere it ships.");
            return;
        }
        if (!ImGui::BeginTable("unshipped", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) return;
        ImGui::TableSetupColumn("Needs");
        ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch);
        for (size_t i = list.size(); i-- > 0;) {
            const Json& c = list.at(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (c["protocol"].boolOr(false)) chip("protocol", kRed);
            const Json& where = c["where"];
            for (size_t w = 0; w < where.size(); ++w) {
                const std::string part = where.at(w).stringOr("");
                ImGui::TextColored(part == "server" ? kRed : part == "client" ? kBlue : part == "launcher" ? kGold : kGreen,
                                   "%s", part.c_str());
            }
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s  %s", c["sha"].stringOr("").c_str(), c["day"].stringOr("").c_str());
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(c["text"].stringOr("").c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(560.0f);
                ImGui::TextUnformatted(c["subject"].stringOr("").c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
        ImGui::EndTable();
    }

    // ---- the output ----

    // The running step's output read into lanes as it comes: `mu ship prepare` and `switch` say
    // each line as "[North Europe] ...", "[windows] ...".
    void parseLanes() {
        if (console_ != lanesOf_) {
            lanes_.clear();
            lanesParsed_ = 0;
            lanesOf_ = console_;
            laneFilter_.clear();
        }
        if (!console_) return;
        const std::string text = console_->output();
        const auto now = Clock::now();
        size_t at = lanesParsed_;
        for (size_t nl = text.find('\n', at); nl != std::string::npos; at = nl + 1, nl = text.find('\n', at)) {
            const std::string line = text.substr(at, nl - at);
            const size_t close = line.find("] ");
            if (line.size() < 4 || line[0] != '[' || close == std::string::npos) continue;
            const std::string label = line.substr(1, close - 1);
            std::string said = line.substr(close + 2);
            const size_t cr = said.rfind('\r');
            if (cr != std::string::npos) said = said.substr(cr + 1);
            Lane* lane = nullptr;
            for (Lane& l : lanes_) {
                if (l.label == label) lane = &l;
            }
            if (!lane) {
                lanes_.push_back({});
                lane = &lanes_.back();
                lane->label = label;
                lane->started = now;
            }
            if (said.rfind("$ ", 0) == 0) lane->state = Lane::State::Running;
            else if (said.rfind("done in ", 0) == 0 || said.rfind("already ", 0) == 0) lane->state = Lane::State::Done, lane->ended = now;
            else if (said.rfind("FAILED", 0) == 0) lane->state = Lane::State::Failed, lane->ended = now;
            else if (said.rfind("the manifest turns in", 0) == 0) lane->state = Lane::State::Waiting;
            if (said.find("players told") != std::string::npos) lane->toldAt = now;
            if (said.find("restarted after") != std::string::npos || said.find("not back in") != std::string::npos) lane->restarted = true;
            if (!said.empty()) lane->lines.push_back(said);
        }
        lanesParsed_ = at;
    }

    void console(float height) {
        ImGui::Separator();
        ImGui::TextColored(kGold, "Output");
        if (!lanes_.empty()) {
            ImGui::SameLine();
            const auto pick = [&](const std::string& label, const char* shown) {
                const bool on = laneFilter_ == label;
                ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(0.30f, 0.33f, 0.40f, 1) : ImVec4(0.13f, 0.14f, 0.17f, 1));
                if (ImGui::SmallButton(shown)) laneFilter_ = label;
                ImGui::PopStyleColor();
                ImGui::SameLine();
            };
            pick("", "All");
            for (const Lane& lane : lanes_) pick(lane.label, lane.label.c_str());
            ImGui::NewLine();
        }
        if (console_) {
            ImGui::SameLine();
            ImGui::TextDisabled("$ %s", console_->command().c_str());
            ImGui::SameLine();
            if (console_->done()) ImGui::TextColored(console_->ok() ? kGreen : kRed, "exit %d", console_->exitCode());
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(console_->output().c_str());
            if (console_->done() && !lastLog_.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("kept in %s", lastLog_.c_str());
            }
        } else {
            ImGui::SameLine();
            ImGui::TextDisabled("a step's output shows here");
        }
        ImGui::BeginChild("console", ImVec2(0, std::max(60.0f, ImGui::GetContentRegionAvail().y)), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_HorizontalScrollbar);
        if (console_) {
            std::string shown;
            if (!laneFilter_.empty()) {
                for (const Lane& lane : lanes_) {
                    if (lane.label != laneFilter_) continue;
                    for (const std::string& l : lane.lines) shown += l + "\n";
                }
            } else {
                // rsync's --progress redraws a line with \r; the last of each is what is shown.
                const std::string text = console_->output();
                shown.reserve(text.size());
                size_t lineStart = 0;
                for (char c : text) {
                    if (c == '\r') shown.resize(lineStart);
                    else {
                        shown += c;
                        if (c == '\n') lineStart = shown.size();
                    }
                }
            }
            const bool atEnd = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f;
            ImGui::TextUnformatted(shown.c_str(), shown.c_str() + shown.size());
            if (atEnd) ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        (void)height;
    }

    // ---- asked first ----

    // What going live does, in its order, with the switch for telling the players.
    void goLiveTimeline() {
        const bool bump = plan_["bump"].boolOr(false);
        const Json& todo = plan_["todo"];
        std::string warners, sudden;
        for (const Json* r : switching()) {
            std::string& into = (warnPlayers_ && (*r)["warns"].boolOr(false)) ? warners : sudden;
            into += (into.empty() ? "" : ", ") + (*r)["name"].stringOr("");
        }
        if (bump) {
            ImGui::TextColored(kRed, "Protocol %d: players on the old game cannot log in from their server's restart until",
                               int(plan_["protocol"].numberOr(0)));
            ImGui::TextColored(kRed, "their launcher has the new client -- seconds, as the manifests turn with the restart.");
            ImGui::Spacing();
        }
        if (!warners.empty() || !sudden.empty()) ImGui::Checkbox("Tell players 60 s before their server restarts", &warnPlayers_);
        ImGui::Spacing();
        if (!ImGui::BeginTable("timeline", 2, ImGuiTableFlags_SizingFixedFit)) return;
        ImGui::TableSetupColumn("when", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthFixed, 560);
        const auto row = [](const char* when, const ImVec4& tone, const std::string& what) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(kDim, "%s", when);
            ImGui::TableNextColumn();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(tone, "%s", what.c_str());
            ImGui::PopTextWrapPos();
        };
        const bool waits = !warners.empty();
        if (waits) row("now", kGold, "Players on " + warners + " are told the server restarts in 60 s.");
        if (!sudden.empty())
            row("now", kRed, sudden + (warnPlayers_ ? " cannot warn yet (an older server): restarted at once." : ": restarted at once."));
        if (waits) row("+60 s", kGold, warners + " restart; the game reconnects its players by itself.");
        std::string clients;
        for (size_t i = 0; i < todo["clients"].size(); ++i) clients += (i ? ", " : "") + todo["clients"].at(i).stringOr("");
        if (!clients.empty()) row(bump && waits ? "+65 s" : "now", kBlue, "The " + clients + " update goes live in the launcher.");
        if (todo["launcher"].boolOr(false)) row("now", kGold, "Every launcher replaces itself with the new one.");
        if (todo["web"].boolOr(false)) row("now", kGreen, "The site restarts on the new commit.");
        row("then", kDim, "Each part read back where it lives; anything not on " + plan_["head"].stringOr("") + " is marked WRONG.");
        ImGui::EndTable();
    }

    void confirmPopup() {
        if (askShip_) {
            askShip_ = false;
            ImGui::OpenPopup("Ship updates?");
        }
        if (ImGui::BeginPopupModal("Ship updates?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextColored(kGold, "Prepare, go live and draft the notes from %s, asked only now;", plan_["head"].stringOr("").c_str());
            ImGui::TextColored(kGold, "the first phase that fails stops the rest.");
            ImGui::Spacing();
            goLiveTimeline();
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.22f, 0.18f, 1));
            if (ImGui::Button("Ship", ImVec2(140, 0))) {
                ImGui::CloseCurrentPopup();
                const int first = nextWaiting(0);
                if (first >= 0) {
                    shipAll_ = runAll_ = true;
                    start(first);
                }
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::Button("Not now", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (confirm_ >= 0 && !ImGui::IsPopupOpen("Go live?")) ImGui::OpenPopup("Go live?");
        if (ImGui::BeginPopupModal("Go live?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (confirm_ >= 0) {
                const Step& s = steps_[size_t(confirm_)];
                if (s.command.rfind("./mu ship switch", 0) == 0) goLiveTimeline();
                else {
                    ImGui::TextColored(kRed, "%s", s.note.c_str());
                    ImGui::Text("%s", s.command.c_str());
                }
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.22f, 0.18f, 1));
                if (ImGui::Button("Go live", ImVec2(140, 0))) {
                    const int i = confirm_;
                    confirm_ = -1;
                    ImGui::CloseCurrentPopup();
                    start(i);
                }
                ImGui::PopStyleColor();
                ImGui::SameLine();
                if (ImGui::Button("Not now", ImVec2(120, 0))) {
                    confirm_ = -1;
                    runAll_ = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
    }

    // ---- the Latest updates ----

    void updatesTab() {
        ImGui::TextColored(kGold, "Drafts from what went live");
        ImGui::SameLine();
        ImGui::TextDisabled("told through %s", notedThrough_.empty() ? "(nothing yet)" : notedThrough_.c_str());
        ImGui::SetNextItemWidth(110);
        ImGui::InputText("day", draftDay_, sizeof draftDay_);
        ImGui::SameLine();
        // From which commit on: where the site got to, or an earlier one to tell a release again.
        ImGui::SetNextItemWidth(90);
        ImGui::InputText("from", since_, sizeof since_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Commits after this one are drafted. It starts as where the site got to (%s);\n"
                              "set it to an earlier commit to draft a release that was not told.",
                              notedThrough_.empty() ? "nothing yet" : notedThrough_.c_str());
        ImGui::SameLine();
        ImGui::Checkbox("one a commit, no claude -p", &plain_);
        ImGui::SameLine();
        const bool drafting = drafting_ && !drafting_->done();
        ImGui::BeginDisabled(drafting);
        if (ImGui::Button("Draft")) draft();
        ImGui::EndDisabled();
        if (drafting) {
            ImGui::SameLine();
            ImGui::TextColored(kDim, "wording... %.0f s", drafting_->seconds());
        }
        if (!draftError_.empty()) ImGui::TextColored(kRed, "%s", draftError_.c_str());
        if (!drafts_.empty()) draftsList();
        else if (drafting_ && drafting_->done() && draftError_.empty())
            ImGui::TextDisabled("Nothing new is live since the last lines were told.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(kGold, "On the site");
        ImGui::SameLine();
        for (const char* f : {"Drafts", "Published", "All"}) {
            const int n = f[0] == 'D' ? 0 : f[0] == 'P' ? 1 : 2;
            const int count = n == 2 ? int(site_.size())
                                     : int(std::count_if(site_.begin(), site_.end(), [&](const Line& l) { return l.published == (n == 1); }));
            char label[32];
            std::snprintf(label, sizeof label, "%s %d", f, count);
            if (ImGui::RadioButton(label, filter_ == n)) filter_ = n;
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("Reload")) loadLines();
        if (sending_ || !outbox_.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(kDim, "saving...");
        } else if (lines_ && !lines_->done()) {
            ImGui::SameLine();
            ImGui::TextColored(kDim, "reading...");
        }
        if (!siteError_.empty()) ImGui::TextColored(kRed, "%s", siteError_.c_str());
        // The first time: the lines written into index.html by hand become the site's own, once.
        if (site_.empty() && notedThrough_.empty() && lines_ && lines_->done() && lines_->ok()) {
            ImGui::TextColored(kGold, "The site keeps no lines yet.");
            ImGui::SameLine();
            const bool importing = importing_ && !importing_->done();
            ImGui::BeginDisabled(importing);
            if (ImGui::Button("Move the page's lines in")) {
                importing_ = std::make_unique<Job>(root_, "./mu ship notes --import-site --json" + webFlag());
                console_ = importing_.get();
            }
            ImGui::EndDisabled();
            if (importing) {
                ImGui::SameLine();
                ImGui::TextColored(kDim, "moving...");
            }
        }
        if (importing_ && importing_->done()) {
            if (!importing_->ok()) siteError_ = lastLines(importing_->output(), 6);
            if (console_ == importing_.get()) console_ = nullptr;
            importing_.reset();
            loadLines();
        }
        ImGui::BeginChild("site", ImVec2(0, 0));
        siteList();
        ImGui::EndChild();
    }

    void kindCombo(Line& l, bool* changed) {
        ImGui::SetNextItemWidth(84);
        if (ImGui::BeginCombo("##kind", kKinds[l.kind])) {
            for (int k = 0; k < 2; ++k) {
                if (ImGui::Selectable(kKinds[k], l.kind == k) && l.kind != k) {
                    l.kind = k;
                    if (changed) *changed = true;
                }
            }
            ImGui::EndCombo();
        }
    }

    void draftsList() {
        const float listHeight = std::min(ImGui::GetContentRegionAvail().y * 0.5f, 34.0f * float(drafts_.size()) + 50.0f);
        ImGui::BeginChild("drafts", ImVec2(0, listHeight), ImGuiChildFlags_Borders);
        for (size_t i = 0; i < drafts_.size(); ++i) {
            Line& l = drafts_[i];
            ImGui::PushID(int(i));
            ImGui::Checkbox("##in", &l.include);
            ImGui::SameLine();
            kindCombo(l, nullptr);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 170);
            ImGui::InputText("##text", l.text, sizeof l.text);
            ImGui::SameLine();
            std::string commits;
            for (const std::string& c : l.commits) commits += (commits.empty() ? "" : " ") + c;
            ImGui::TextDisabled("%s", commits.c_str());
            ImGui::PopID();
        }
        ImGui::EndChild();
        const int chosen = int(std::count_if(drafts_.begin(), drafts_.end(), [](const Line& l) { return l.include; }));
        ImGui::BeginDisabled(sending_ != nullptr);
        if (ImGui::Button("Send as drafts")) sendDrafts(false);
        ImGui::SameLine();
        if (ImGui::Button("Publish now")) sendDrafts(true);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Discard")) drafts_.clear();
        ImGui::SameLine();
        ImGui::TextDisabled("%d of %zu lines go; told through %s after", chosen, drafts_.size(), draftsThrough_.c_str());
    }

    void sendDrafts(bool publish) {
        std::string body = "{\"entries\":[";
        bool first = true;
        for (Line& l : drafts_) {
            if (!l.include) continue;
            std::snprintf(l.day, sizeof l.day, "%s", draftDay_);
            body += (first ? "" : ",") + lineJson(l, false, publish);
            first = false;
        }
        body += "],\"noted_through\":" + jsonString(draftsThrough_) + "}";
        outbox_.push_back(body);
        drafts_.clear();
    }

    void siteList() {
        std::string day;
        for (size_t i = 0; i < site_.size(); ++i) {
            Line& l = site_[i];
            if (filter_ == 0 && l.published) continue;
            if (filter_ == 1 && !l.published) continue;
            if (day != l.day) {
                day = l.day;
                ImGui::Spacing();
                ImGui::TextColored(kGold, "%s", longDay(day).c_str());
                ImGui::SameLine();
                int drafts = 0;
                for (const Line& o : site_) drafts += o.day == day && !o.published;
                if (drafts) {
                    ImGui::PushID(day.c_str());
                    char label[40];
                    std::snprintf(label, sizeof label, "Publish all %d", drafts);
                    if (ImGui::SmallButton(label)) {
                        std::string body = "{\"entries\":[";
                        bool first = true;
                        for (Line& o : site_) {
                            if (o.day != day || o.published) continue;
                            o.published = true;
                            body += (first ? "" : ",") + lineJson(o, true, true);
                            first = false;
                        }
                        outbox_.push_back(body + "]}");
                    }
                    ImGui::PopID();
                }
            }
            ImGui::PushID(l.id);
            bool changed = false;
            kindCombo(l, &changed);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 190);
            ImGui::InputText("##text", l.text, sizeof l.text);
            if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, l.published ? ImVec4(0.17f, 0.36f, 0.24f, 1) : ImGui::GetStyle().Colors[ImGuiCol_Button]);
            if (ImGui::Button(l.published ? "Published" : "Publish", ImVec2(84, 0))) {
                l.published = !l.published;
                changed = true;
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(l.published ? "Take it off the front page" : "Show it on the front page");
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, l.armed ? ImVec4(0.55f, 0.20f, 0.17f, 1) : ImVec4(0.16f, 0.17f, 0.20f, 1));
            if (ImGui::Button(l.armed ? "Sure?" : "Delete", ImVec2(64, 0))) {
                if (l.armed) outbox_.push_back("{\"entries\":[],\"delete\":[" + std::to_string(l.id) + "]}");
                l.armed = !l.armed;
            }
            ImGui::PopStyleColor();
            if (changed) saveLine(l);
            ImGui::PopID();
        }
        if (site_.empty() && lines_ && lines_->done()) ImGui::TextDisabled("No lines.");
    }

    std::string root_, web_;
    std::string wantTab_;
    std::unique_ptr<Job> status_, lines_, drafting_, sending_, step_, importing_, logJob_;
    bool statusTaken_ = false, linesTaken_ = false, draftTaken_ = false;
    Json plan_;
    std::string statusError_, siteError_, draftError_;
    std::string lastLog_, failure_;
    Clock::time_point checkedAt_ = Clock::now();
    std::vector<Step> steps_;
    int running_ = -1, confirm_ = -1;
    bool runAll_ = false, needsRefresh_ = false;
    bool shipAll_ = false;   // Ship updates: asked once for every step, so no LIVE one asks again
    bool askShip_ = false;   // its popup is to open
    Job* console_ = nullptr;
    std::vector<Line> site_, drafts_;
    std::string notedThrough_, draftsThrough_;
    std::vector<std::string> outbox_;
    char draftDay_[16] = {};
    char since_[48] = {};
    std::string autoDraft_;
    bool plain_ = false;
    int filter_ = 0;
    std::vector<Lane> lanes_;
    const Job* lanesOf_ = nullptr;
    size_t lanesParsed_ = 0;
    std::string laneFilter_;   // the lane the output shows, "" all
    bool warnPlayers_ = true;  // Go live tells the players 60 s first; off adds --now
};

void style() {
    ImGuiStyle& st = ImGui::GetStyle();
    ImGui::StyleColorsDark(&st);
    st.WindowPadding = ImVec2(16, 14);
    st.FrameRounding = 4.0f;
    st.ChildRounding = 4.0f;
    st.CellPadding = ImVec2(8, 5);
    st.ItemSpacing = ImVec2(8, 6);
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.067f, 0.075f, 0.09f, 1.0f);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.11f, 0.14f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.55f, 0.42f, 0.16f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.70f, 0.54f, 0.21f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.80f, 0.62f, 0.25f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.16f, 0.18f, 0.22f, 1.0f);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.10f, 0.11f, 0.14f, 1.0f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.025f);
    c[ImGuiCol_Tab] = ImVec4(0.10f, 0.11f, 0.14f, 1.0f);
    c[ImGuiCol_TabSelected] = ImVec4(0.20f, 0.22f, 0.27f, 1.0f);
    c[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.28f, 0.34f, 1.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.10f, 0.12f, 1.0f);
}

}  // namespace

int main(int argc, char** argv) {
    std::string root = MU2_SOURCE_DIR, web, shot, tab, draftFrom, log;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--web" && i + 1 < argc) web = argv[++i];
        else if (a == "--shot" && i + 1 < argc) shot = argv[++i];
        else if (a == "--tab" && i + 1 < argc) tab = argv[++i];
        else if (a == "--draft-from" && i + 1 < argc) draftFrom = argv[++i];
        else if (a == "--log" && i + 1 < argc) log = argv[++i];
        else {
            std::fprintf(stderr, "usage: mu_ship [--web URL] [--tab release|updates] [--log FILE] [--shot PNG]\n");
            return 2;
        }
    }

    mu::gfx::Window window;
    mu::gfx::WindowDesc desc;
    desc.width = 1500;
    desc.height = 950;
    desc.vsync = true;
    desc.title = "Ship";
    if (!window.open(desc)) return 1;
    mu::gfx::Gui gui;
    if (!gui.open(window, root + "/third_party/extern/OpenSans-SemiBold.ttf", 15.0f)) return 1;
    style();
    std::signal(SIGTERM, onSignal);
    std::signal(SIGINT, onSignal);
    std::signal(SIGHUP, onSignal);
    {
        App app(root, web);
        app.refresh();
        if (!tab.empty()) app.openTab(tab);
        if (!draftFrom.empty()) app.draftFrom(draftFrom);
        if (!log.empty()) app.showLog(log);
        auto last = Clock::now();
        Clock::time_point shotAt{};
        int shotFrames = -1;
        // Drawn when something changes: input, a child's output, or once a second for the clocks.
        double activeUntil = 0.0;
        for (;;) {
            const double waitFrom = glfwGetTime();
            const bool fast = waitFrom < activeUntil || !shot.empty();
            if (!fast) glfwWaitEventsTimeout(app.busy() ? 0.25 : 1.0);
            if (!window.pump() || g_quit) break;
            if (!fast) activeUntil = glfwGetTime() + 0.4;
            app.poll();
            if (!shot.empty() && shotFrames < 0) {
                if (app.statusIn() && shotAt == Clock::time_point{}) shotAt = Clock::now() + std::chrono::seconds(4);
                if (shotAt != Clock::time_point{} && Clock::now() > shotAt && !app.busy()) {
                    bgfx::requestScreenShot(BGFX_INVALID_HANDLE, shot.c_str());
                    shotFrames = 3;
                }
            } else if (shotFrames > 0 && --shotFrames == 0) {
                break;
            }
            const auto now = Clock::now();
            const float dt = std::chrono::duration<float>(now - last).count();
            last = now;
            bgfx::setViewRect(0, 0, 0, uint16_t(window.width()), uint16_t(window.height()));
            bgfx::setViewClear(0, BGFX_CLEAR_COLOR, 0x111317ff);
            bgfx::touch(0);
            gui.begin(window, dt);
            app.draw(window.width(), window.height());
            gui.end();
            bgfx::frame();
        }
    }
    gui.close();
    window.close();
    return 0;
}
