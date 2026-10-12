// The studio driven by words: StudioDesk::act, the one list of what MU_STUDIO_DO and the
// control socket may ask, and the socket itself (docs/order-plan.md §9c, F7).
//
//   subject <tab>|<name>   open a tab of the browser and put a name of it on the bench
//   scene <Stage|Home|w>   stand it there          plus <N>     the shine at +N
//   swap                   Read again              arena        Simulate combat
//   place | pick <model> | nudge <dx> <dz>          Place objects, as the window does
//   add <model> | duplicate | delete                 ... and sprint G's: add at the view's middle
//   mode objects|gates|townsfolk                     what Place places; duplicate and delete follow it
//   mark <n> | budge <dc> <dr> | look <n>            a gate by number or townsperson by index; tiles
//   tab light              the inspector's Light   hide | show  the panels off and on
//   shot <abs.png>         this frame to a file    wait <N>     N frames before the next line
//   status                 what is on the bench    quit         close the studio
//
// The socket is workshop/.mu/studio.sock: a line in, a line out, "ok ..." or "error ...".
// Lines from one client run in order, a frame apart at least, and `wait` holds that client's
// next line back -- a scene change lands on the next frame and a shot is written when the
// frame is, so a script waits before it reads a picture it asked for.
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>

#include <bgfx/bgfx.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "app/context.h"
#include "app/modes/studio_desk.h"
#include "core/log.h"
#include "game/bench.h"
#include "game/figures.h"

namespace mu::app {

std::string StudioDesk::act(Context& ctx, game::ModelBench& bench, const std::string& world,
                            game::World& town, const std::string& what) {
    auto after = [&](const char* word) { return what.substr(std::string(word).size() + 1); };
    auto starts = [&](const char* word) {
        const size_t n = std::string(word).size();
        return what.size() > n && what.compare(0, n, word) == 0 && what[n] == ' ';
    };
    if (what == "swap") {
        if (swap_ != Swap::Idle) return "error a swap is being shot";
        startSwap(bench, bench.browseName(bench.browseIndex()));
        return "ok swapping " + bench.browseName(bench.browseIndex());
    }
    if (starts("plus")) {
        plusAsked_ = std::atoi(after("plus").c_str());
        return "ok +" + std::to_string(plusAsked_);
    }
    if (what == "tab light") {
        panels_.showLight();
        return "ok the Light tab";
    }
    if (what == "arena") {
        if (!bench.subjectBody() || bench.subjectBody()->kind != game::BodyKind::Monster) {
            return "error no monster on the bench";
        }
        openArena(bench.subjectBody()->name, world);
        return "ok the arena for " + bench.subjectBody()->name;
    }
    if (what == "place") {
        placing_ = true;
        return "ok placing in " + world;
    }
    if (starts("pick")) {
        float near[3];
        for (int a = 0; a < 3; ++a) near[a] = bench.camera().target[a];
        place_.world(ctx, world);
        if (!place_.pickModel(town, after("pick"), near)) return "error no " + after("pick") + " here";
        return "ok instance " + std::to_string(place_.picked()) + ", source line " +
               std::to_string(place_.line());
    }
    if (starts("nudge")) {
        float dx = 0.0f, dz = 0.0f;
        if (std::sscanf(after("nudge").c_str(), "%f %f", &dx, &dz) != 2) return "error nudge <dx> <dz>";
        if (place_.line() < 0) return "error nothing picked that the source holds";
        place_.nudge(ctx, town, cooker_, dx, dz);
        return "ok nudged";
    }
    if (starts("mode")) {
        const std::string mode = after("mode");
        const int asked = mode == "objects" ? 0 : mode == "gates" ? 1 : mode == "townsfolk" ? 2 : -1;
        if (asked < 0) return "error mode objects|gates|townsfolk";
        placing_ = true;
        placeMode_ = asked;
        place_.drop();
        marks_.drop();
        return "ok placing " + mode + " in " + world;
    }
    if (placeMode_ != 0 && (starts("mark") || starts("budge") || starts("look") || what == "duplicate" ||
                            what == "delete")) {
        marks_.world(ctx, world);
        marks_.kind(ctx, placeMode_ == 1 ? StudioMarks::Kind::Gates : StudioMarks::Kind::Townsfolk);
        std::string answer;
        bool ok = false;
        int a = 0, b = 0;
        if (starts("mark")) {
            ok = marks_.pick(std::atoi(after("mark").c_str()));
            answer = ok ? "picked " + after("mark") : "no " + after("mark") + " here";
        } else if (starts("budge")) {
            if (std::sscanf(after("budge").c_str(), "%d %d", &a, &b) != 2) return "error budge <dc> <dr>";
            ok = marks_.budge(ctx, cooker_, a, b, &answer);
        } else if (starts("look")) {
            ok = marks_.look(ctx, cooker_, std::atoi(after("look").c_str()), &answer);
        } else {
            ok = what == "duplicate" ? marks_.duplicate(ctx, cooker_, &answer) : marks_.remove(ctx, cooker_, &answer);
        }
        return (ok ? "ok " : "error ") + answer;
    }
    if (starts("add") || what == "duplicate" || what == "delete") {
        place_.world(ctx, world);
        std::string answer;
        const bool ok = starts("add") ? place_.add(town, cooker_, after("add"), bench.camera().target[0],
                                                   bench.camera().target[2], &answer)
                        : what == "duplicate" ? place_.duplicate(town, cooker_, &answer)
                                              : place_.remove(town, cooker_, &answer);
        return (ok ? "ok " : "error ") + answer;
    }
    if (starts("scene")) {
        sceneAsked_ = after("scene");
        for (size_t i = 0; i < shelf_.scenes.size(); ++i) {
            if (shelf_.scenes[i] == sceneAsked_) shelf_.scene = int(i);
        }
        shelfCategory_ = -1;
        return "ok scene " + sceneAsked_;
    }
    if (starts("subject")) {
        const std::string both = after("subject");
        const size_t bar = both.find('|');
        if (bar == std::string::npos) return "error subject <tab>|<name>";
        if (!bench.openCategory(both.substr(0, bar), ctx.textures)) return "error no tab " + both.substr(0, bar);
        if (!bench.pick(both.substr(bar + 1), ctx.textures)) return "error no " + both.substr(bar + 1);
        shelfCategory_ = -1;
        lastSwap_.clear();
        return "ok " + bench.browseName(bench.browseIndex());
    }
    if (starts("shot")) {
        const std::string path = after("shot");
        if (path.empty() || path[0] != '/') return "error shot takes an absolute path";
        bgfx::requestScreenShot(BGFX_INVALID_HANDLE, path.c_str());
        return "ok " + path + " when this frame is drawn";
    }
    if (what == "hide" || what == "show") {
        hidden_ = what == "hide";
        return "ok panels " + what;
    }
    if (what == "status") {
        const std::string scene = shelf_.scene >= 0 && shelf_.scene < int(shelf_.scenes.size())
                                      ? shelf_.scenes[size_t(shelf_.scene)]
                                      : std::string("?");
        return "ok " + bench.browseLine() + " | scene " + scene + " | world " + world +
               " | make " + targetOf(bench) + (cooker_.connected() ? " | mu serve up" : " | mu serve down");
    }
    if (what == "quit") {
        quit_ = true;
        return "ok closing";
    }
    return "error no such word: " + what;
}

void StudioDesk::openControl(Context& ctx) {
    controlPath_ = ctx.paths.root + "/workshop/.mu/studio.sock";
    ::unlink(controlPath_.c_str());   // one studio at a time listens; the last one opened wins
    listener_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::snprintf(address.sun_path, sizeof(address.sun_path), "%s", controlPath_.c_str());
    if (listener_ < 0 || ::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(listener_, 4) != 0) {
        core::logError("studio: no control socket at %s", controlPath_.c_str());
        if (listener_ >= 0) ::close(listener_);
        listener_ = -1;
        return;
    }
    ::fcntl(listener_, F_SETFL, ::fcntl(listener_, F_GETFL) | O_NONBLOCK);
    core::logf("studio: listening on %s (./mu do \"...\")", controlPath_.c_str());
}

void StudioDesk::serveControl(Context& ctx, game::ModelBench& bench, const std::string& world,
                              game::World& town) {
    if (listener_ < 0) return;
    for (;;) {
        const int fd = ::accept(listener_, nullptr, nullptr);
        if (fd < 0) break;
        ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
        int one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
        clients_.push_back({fd, "", 0});
    }
    for (Client& client : clients_) {
        char chunk[1024];
        for (;;) {
            const ssize_t n = ::recv(client.fd, chunk, sizeof(chunk), 0);
            if (n > 0) {
                client.in.append(chunk, size_t(n));
                continue;
            }
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                ::close(client.fd);
                client.fd = -1;
            }
            break;
        }
        // One line a frame a client, and none while it waits.
        if (client.fd < 0 || frame_ < client.waitUntil) continue;
        const size_t end = client.in.find('\n');
        if (end == std::string::npos) continue;
        std::string line = client.in.substr(0, end);
        client.in.erase(0, end + 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        while (!line.empty() && line.front() == ' ') line.erase(0, 1);
        std::string reply;
        if (line.compare(0, 5, "wait ") == 0) {
            client.waitUntil = frame_ + std::max(1, std::atoi(line.c_str() + 5));
            reply = "ok waited";
        } else {
            reply = act(ctx, bench, world, town, line);
            client.waitUntil = frame_ + 1;
        }
        core::logf("studio: '%s' -> %s", line.c_str(), reply.c_str());
        reply += "\n";
        if (::send(client.fd, reply.data(), reply.size(), 0) < 0) {
            ::close(client.fd);
            client.fd = -1;
        }
    }
    clients_.erase(std::remove_if(clients_.begin(), clients_.end(),
                                  [](const Client& c) { return c.fd < 0; }),
                   clients_.end());
}

void StudioDesk::closeControl() {
    for (Client& client : clients_) ::close(client.fd);
    clients_.clear();
    if (listener_ >= 0) {
        ::close(listener_);
        ::unlink(controlPath_.c_str());
    }
    listener_ = -1;
}

}  // namespace mu::app
