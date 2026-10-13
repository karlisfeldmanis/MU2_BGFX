#include "studio/cooker.h"

#include <cerrno>
#include <chrono>
#include <cstring>

#include <fcntl.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "core/json.h"
#include "core/log.h"
#include "core/paths.h"

extern char** environ;

namespace mu::studio {
namespace {

double wallSeconds() {
    using namespace std::chrono;
    return duration<double>(system_clock::now().time_since_epoch()).count();
}

// A JSON string, quoted and escaped: the requests carry a recipe's name and nothing stranger.
std::string quoted(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        if (static_cast<unsigned char>(c) >= 0x20) out += c;
    }
    return out + "\"";
}

// A target as `mu make`'s words: "placements lorencia" is two, as on the command line, and
// make.py's plan reads them apart (one string was "no recipe called placements lorencia").
std::string words(const std::string& target) {
    std::string out;
    for (size_t start = 0; start < target.size();) {
        size_t end = target.find(' ', start);
        if (end == std::string::npos) end = target.size();
        if (end > start) out += (out.empty() ? "" : ", ") + quoted(target.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

int openSocket(const std::string& path) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(fd);
        return -1;
    }
    // A request is answered at once; five seconds is a server that is not answering.
    timeval wait{5, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
    return fd;
}

bool sendAll(int fd, const std::string& text) {
    size_t sent = 0;
    while (sent < text.size()) {
        const ssize_t n = ::send(fd, text.data() + sent, text.size() - sent, 0);
        if (n <= 0) return false;
        sent += size_t(n);
    }
    return true;
}

StepView stepFrom(const core::Json& j) {
    StepView s;
    s.name = j["name"].stringOr("");
    s.state = j["state"].stringOr("waiting");
    s.estimate = j["estimate"].numberOr(-1.0);
    s.seconds = j["seconds"].numberOr(-1.0);
    s.began = j["began"].numberOr(0.0);
    return s;
}

}  // namespace

// ---------------------------------------------------------------------------- the views

double JobView::eta(double now) const {
    if (finished()) return 0.0;
    double left = 0.0;
    for (size_t i = 0; i < steps.size(); ++i) {
        const StepView& s = steps[i];
        if (s.state == "done" || s.state == "skipped") continue;
        double guess = s.estimate;
        if (guess < 0.0) return -1.0;
        if (int(i) == at && s.began > 0.0) {
            const double ran = now - s.began;
            if (of > 0 && n > 0) guess = ran / (double(n) / double(of));
            left += guess > ran ? guess - ran : 0.0;
        } else {
            left += guess;
        }
    }
    return left;
}

bool PlanView::stale() const {
    for (const Step& s : steps) {
        if (s.needed) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------- the connection

Cooker::~Cooker() { close(); }

void Cooker::open(const std::string& root) {
    root_ = root;
    socket_ = core::folders().work + "/.mu/serve.sock";
}

void Cooker::close() {
    if (planThread_ && planThread_->joinable()) planThread_->join();
    planThread_.reset();
    if (recipeThread_ && recipeThread_->joinable()) recipeThread_->join();
    recipeThread_.reset();
    if (stream_ >= 0) ::close(stream_);
    stream_ = -1;
}

bool Cooker::request(const std::string& line, std::string* reply) {
    const int fd = openSocket(socket_);
    if (fd < 0) {
        error_ = "mu serve is not running";
        return false;
    }
    bool ok = sendAll(fd, line + "\n");
    std::string got;
    char chunk[4096];
    while (ok && got.find('\n') == std::string::npos) {
        const ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            ok = false;
            break;
        }
        got.append(chunk, size_t(n));
    }
    ::close(fd);
    if (!ok) {
        error_ = "mu serve did not answer";
        return false;
    }
    *reply = got.substr(0, got.find('\n'));
    return true;
}

bool Cooker::connect() {
    // What is running now, then every event after it: the jobs' views first, so a studio
    // opened in the middle of a cook shows that cook's bar from its first frame.
    std::string reply;
    if (!request("{\"op\": \"jobs\"}", &reply)) return false;
    const core::Json answer = core::parseJson(reply.data(), reply.size(), "mu serve's jobs");
    jobs_.clear();
    for (const core::Json& view : answer["jobs"].items) load(view);
    const core::Json& cores = answer["cores"];
    cores_.count = int(cores["count"].numberOr(0.0));
    cores_.machine = int(cores["machine"].numberOr(0.0));
    cores_.from = cores["from"].stringOr("");
    const long long since = (long long)answer["seq"].numberOr(0.0);

    stream_ = openSocket(socket_);
    if (stream_ < 0) return false;
    if (!sendAll(stream_, "{\"op\": \"watch\", \"job\": null, \"since\": " + std::to_string(since) +
                              "}\n")) {
        ::close(stream_);
        stream_ = -1;
        return false;
    }
    ::fcntl(stream_, F_SETFL, ::fcntl(stream_, F_GETFL) | O_NONBLOCK);
    buffer_.clear();
    core::logf("studio: mu serve connected, %zu jobs known", jobs_.size());
    return true;
}

void Cooker::poll() {
    if (serverPid_ > 0) {
        int status = 0;
        if (::waitpid(serverPid_, &status, WNOHANG) == serverPid_) {
            core::logf("studio: the mu serve it started has ended");
            serverPid_ = 0;
        }
    }
    const double now = wallSeconds();
    if (stream_ < 0) {
        if (now < nextTry_) return;
        // Every half second while a server this studio started is coming up, every two after.
        nextTry_ = now + (serverPid_ > 0 && !wasConnected_ ? 0.5 : 2.0);
        if (!connect()) {
            // A server that was here and went -- stopped for new code, or by hand -- is
            // started again, no more often than every ten seconds.
            if (wasConnected_ && serverPid_ == 0 && now > nextStart_) {
                nextStart_ = now + 10.0;
                startServer();
            }
            return;
        }
        wasConnected_ = true;
    }
    char chunk[8192];
    for (;;) {
        const ssize_t n = ::recv(stream_, chunk, sizeof(chunk), 0);
        if (n > 0) {
            buffer_.append(chunk, size_t(n));
            continue;
        }
        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
            core::logf("studio: mu serve went away");
            ::close(stream_);
            stream_ = -1;
            nextTry_ = now + 2.0;
        }
        break;
    }
    size_t start = 0;
    for (size_t end; (end = buffer_.find('\n', start)) != std::string::npos; start = end + 1) {
        if (end > start) take(core::parseJson(buffer_.data() + start, end - start, "a mu serve event"));
    }
    buffer_.erase(0, start);
}

bool Cooker::startServer() {
    if (serverPid_ > 0) return true;
    if (root_.empty()) {
        error_ = "the studio was not told where mu is";
        return false;
    }
    const std::string mu = root_ + "/mu";
    const std::string logs = core::folders().work + "/logs";
    const std::string log = logs + "/mu/serve.log";
    ::mkdir(logs.c_str(), 0755);
    ::mkdir((logs + "/mu").c_str(), 0755);
    posix_spawn_file_actions_t files;
    posix_spawn_file_actions_init(&files);
    posix_spawn_file_actions_addopen(&files, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&files, 1, log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    posix_spawn_file_actions_adddup2(&files, 1, 2);
    posix_spawn_file_actions_addchdir(&files, root_.c_str());
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    // Its own session: the server is not the studio's child in the sense that matters -- a
    // Ctrl-C in the studio's terminal does not reach it, and closing the studio leaves it cooking.
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSID);
    char* argv[] = {const_cast<char*>(mu.c_str()), const_cast<char*>("serve"), nullptr};
    pid_t pid = 0;
    const int failed = posix_spawn(&pid, mu.c_str(), &files, &attributes, argv, environ);
    posix_spawn_file_actions_destroy(&files);
    posix_spawnattr_destroy(&attributes);
    if (failed != 0) {
        error_ = std::string("could not start mu serve: ") + std::strerror(failed);
        core::logError("studio: %s", error_.c_str());
        return false;
    }
    serverPid_ = pid;
    nextTry_ = 0.0;
    core::logf("studio: started mu serve (pid %d), its log in workshop/logs/mu/serve.log", pid);
    return true;
}

int Cooker::make(const std::string& target) {
    std::string reply;
    if (!request("{\"op\": \"make\", \"target\": [" + words(target) + "]}", &reply)) return 0;
    const core::Json answer = core::parseJson(reply.data(), reply.size(), "mu serve's make");
    const int number = int(answer["job"].numberOr(0.0));
    if (number == 0) error_ = answer["error"].stringOr("mu serve refused the job");
    return number;
}

bool Cooker::cancel(int number, bool now) {
    std::string reply;
    return request("{\"op\": \"cancel\", \"job\": " + std::to_string(number) +
                       ", \"now\": " + (now ? "true" : "false") + "}",
                   &reply);
}

void Cooker::askPlan(const std::string& target) {
    {
        std::lock_guard<std::mutex> hold(planLock_);
        if (plan_.target == target) return;
        plan_ = PlanView{};
        plan_.target = target;
    }
    if (planThread_ && planThread_->joinable()) planThread_->join();
    planThread_ = std::make_unique<std::thread>([this, target] {
        PlanView answer;
        answer.target = target;
        std::string reply;
        if (!request("{\"op\": \"plan\", \"target\": [" + words(target) + "]}", &reply)) {
            answer.error = "mu serve is not running";
        } else {
            const core::Json j = core::parseJson(reply.data(), reply.size(), "mu serve's plan");
            answer.error = j["error"].stringOr("");
            for (const core::Json& s : j["steps"].items) {
                answer.steps.push_back({s["name"].stringOr(""), s["needed"].boolOr(false),
                                        s["reason"].stringOr("")});
            }
        }
        answer.answered = true;
        std::lock_guard<std::mutex> hold(planLock_);
        if (plan_.target == target) plan_ = std::move(answer);   // not one asked since
    });
}

void Cooker::askRecipe(const std::string& target) {
    {
        std::lock_guard<std::mutex> hold(planLock_);
        if (recipe_.target == target) return;
        recipe_ = RecipeView{};
        recipe_.target = target;
    }
    if (recipeThread_ && recipeThread_->joinable()) recipeThread_->join();
    recipeThread_ = std::make_unique<std::thread>([this, target] {
        RecipeView answer;
        answer.target = target;
        std::string reply;
        if (!request("{\"op\": \"recipe\", \"target\": " + quoted(target) + "}", &reply)) {
            answer.error = "mu serve is not running";
        } else {
            const core::Json j = core::parseJson(reply.data(), reply.size(), "mu serve's recipe");
            answer.error = j["error"].stringOr("");
            for (const core::Json& f : j["files"].items) {
                answer.files.push_back({f["file"].stringOr(""), f["doc"]});
            }
            for (const core::Json& m : j["materials"].items) answer.materials.push_back(m.stringOr(""));
        }
        answer.answered = true;
        std::lock_guard<std::mutex> hold(planLock_);
        if (recipe_.target == target) recipe_ = std::move(answer);
    });
}

RecipeView Cooker::recipe() const {
    std::lock_guard<std::mutex> hold(planLock_);
    return recipe_;
}

void Cooker::forgetRecipe() {
    std::lock_guard<std::mutex> hold(planLock_);
    recipe_.target.clear();
}

bool Cooker::edit(const std::string& file, const std::string& path, const std::string& value,
                  std::string* answer, const char* how) {
    std::string reply;
    if (!request("{\"op\": \"edit\", \"how\": \"" + std::string(how) + "\", \"file\": " + quoted(file) +
                     ", \"path\": " + path + ", \"value\": " + (value.empty() ? "null" : value) +
                     ", \"who\": \"studio\"}",
                 &reply)) {
        *answer = error_;
        return false;
    }
    const core::Json j = core::parseJson(reply.data(), reply.size(), "mu serve's edit");
    if (!j["error"].isNull()) {
        *answer = j["error"].stringOr("refused");
        return false;
    }
    const core::Json& line = j["edit"];
    *answer = line["path"].stringOr("") + " written to " + line["file"].stringOr("");
    return true;
}

bool Cooker::gate(const std::string& fields, std::string* answer, int* number) {
    std::string reply;
    if (!request("{\"op\": \"gate\", " + fields + ", \"who\": \"studio\"}", &reply)) {
        *answer = error_;
        return false;
    }
    const core::Json j = core::parseJson(reply.data(), reply.size(), "mu serve's gate");
    if (!j["error"].isNull()) {
        *answer = j["error"].stringOr("refused");
        return false;
    }
    const core::Json& done = j["gate"];
    if (number) *number = int(done["enter"].numberOr(0.0));
    if (!done["enter"].isNull()) {
        *answer = "gate " + std::to_string(int(done["enter"].number)) + " leads to gate " +
                  std::to_string(int(done["exit"].numberOr(0.0))) + ", its way out on the arrival tile there";
    } else {
        *answer = "removed";
        for (size_t i = 0; i < done["removed"].size(); ++i) {
            *answer += (i ? ", " : " ") + done["removed"].at(i).stringOr("");
        }
    }
    return true;
}

bool Cooker::paint(const std::string& fields, std::string* answer) {
    std::string reply;
    if (!request("{\"op\": \"paint\", " + fields + ", \"who\": \"studio\"}", &reply)) {
        *answer = error_;
        return false;
    }
    const core::Json j = core::parseJson(reply.data(), reply.size(), "mu serve's paint");
    if (!j["error"].isNull()) {
        *answer = j["error"].stringOr("refused");
        return false;
    }
    *answer = std::to_string(int(j["paint"].numberOr(0.0))) + " tiles written to tiles.png";
    return true;
}

void Cooker::forgetPlan() {
    std::lock_guard<std::mutex> hold(planLock_);
    plan_.target.clear();
}

PlanView Cooker::plan() const {
    std::lock_guard<std::mutex> hold(planLock_);
    return plan_;
}

// ---------------------------------------------------------------------------- the events

JobView* Cooker::job(int number) {
    for (JobView& j : jobs_) {
        if (j.number == number) return &j;
    }
    return nullptr;
}

void Cooker::load(const core::Json& view) {
    JobView j;
    j.number = int(view["job"].numberOr(0.0));
    j.target = view["target"].stringOr("");
    j.state = view["state"].stringOr("queued");
    for (const core::Json& s : view["steps"].items) {
        const int index = int(s["index"].numberOr(-1.0));
        if (index >= 0) {
            if (int(j.planIndex.size()) <= index) j.planIndex.resize(size_t(index) + 1, -1);
            j.planIndex[size_t(index)] = int(j.steps.size());
        }
        j.steps.push_back(stepFrom(s));
    }
    j.at = int(view["at"].numberOr(-1.0));
    const core::Json& progress = view["progress"];
    j.n = int(progress["n"].numberOr(0.0));
    j.of = int(progress["of"].numberOr(0.0));
    j.label = progress["label"].stringOr("");
    j.line = view["line"].stringOr("");
    j.error = view["error"].stringOr("");
    j.started = view["started"].numberOr(0.0);
    j.ended = view["ended"].numberOr(0.0);
    j.cores = int(view["cores"].numberOr(0.0));
    jobs_.push_back(std::move(j));
}

void Cooker::take(const core::Json& event) {
    const std::string kind = event["kind"].stringOr("");
    const int number = int(event["job"].numberOr(0.0));
    const double at = event["at"].numberOr(0.0);
    if (kind == "queued") {
        if (job(number)) return;
        JobView j;
        j.number = number;
        for (const core::Json& word : event["target"].items) {
            j.target += (j.target.empty() ? "" : " ") + word.stringOr("");
        }
        j.state = "queued";
        jobs_.push_back(std::move(j));
        if (jobs_.size() > 50) jobs_.erase(jobs_.begin());
        return;
    }
    JobView* j = job(number);
    if (!j) return;
    // The event names the plan's step; the bar holds only the steps that run.
    const int planStep = int(event["step"].numberOr(-1.0));
    const int i = planStep >= 0 && planStep < int(j->planIndex.size()) ? j->planIndex[size_t(planStep)] : -1;
    if (kind == "plan") {
        j->state = "running";
        j->started = at;
        j->cores = int(event["cores"].numberOr(0.0));
        if (j->cores > 0) cores_.count = j->cores;
        if (event["machine"].numberOr(0.0) > 0.0) cores_.machine = int(event["machine"].numberOr(0.0));
        j->steps.clear();
        j->planIndex.clear();
        for (const core::Json& s : event["steps"].items) {
            const int index = int(s["index"].numberOr(-1.0));
            if (index >= 0) {
                if (int(j->planIndex.size()) <= index) j->planIndex.resize(size_t(index) + 1, -1);
                j->planIndex[size_t(index)] = int(j->steps.size());
            }
            j->steps.push_back(stepFrom(s));
        }
    } else if (kind == "step" && i >= 0) {
        j->at = i;
        j->n = j->of = 0;
        j->label.clear();
        j->line.clear();
        j->steps[size_t(i)].state = "running";
        j->steps[size_t(i)].began = at;
    } else if (kind == "progress") {
        j->n = int(event["n"].numberOr(0.0));
        j->of = int(event["of"].numberOr(0.0));
        j->label = event["label"].stringOr("");
    } else if (kind == "line" || kind == "say") {
        j->line = event["text"].stringOr("");
    } else if (kind == "done" && i >= 0) {
        j->steps[size_t(i)].state = event["code"].numberOr(1.0) == 0.0 ? "done" : "failed";
        j->steps[size_t(i)].seconds = event["seconds"].numberOr(-1.0);
        j->n = j->of = 0;
    } else if (kind == "finished") {
        const bool ok = event["ok"].boolOr(false);
        j->state = event["state"].stringOr(ok ? "done" : "failed");
        j->error = event["error"].stringOr("");
        j->ended = at;
        if (j->started == 0.0) j->started = at;
        for (StepView& s : j->steps) {
            if (s.state == "waiting" || s.state == "running") s.state = ok ? "skipped" : "not run";
        }
    }
}

}  // namespace mu::studio
