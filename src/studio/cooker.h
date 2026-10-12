// The studio's end of `mu serve`, the cook's job runner (tools/mu/serve.py, docs/order-plan.md
// §9c). No ImGui and no bgfx here, so cooker_test drives it with no window.
//
// The server runs the jobs; this keeps a copy of what they are doing, from the events it
// streams on workshop/.mu/serve.sock, and sends it the three things a panel asks: make this,
// cancel that, and what would a make of this run (the stale marker). Nothing here waits on a
// cook. poll() is called once a frame and reads what has arrived; a plan, which runs the
// cook's date tests, is asked on a thread of its own and picked up when it is answered.
//
// If no server answers, the studio starts one (startServer), as `./mu serve` with its output
// in workshop/logs/mu/serve.log. It outlives the studio: a cook is never ended by closing a
// window, the same rule `mu make`'s Ctrl-C keeps.
#pragma once

#include <memory>
#include <mutex>

#include "core/json.h"
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace mu::studio {

struct StepView {
    std::string name;
    std::string state;      // waiting, running, done, failed, skipped, not run
    double estimate = -1.0; // seconds, from workshop/.mu/timings.json; -1 never timed
    double seconds = -1.0;  // how long it took, once done
    double began = 0.0;     // wall seconds
};

struct JobView {
    int number = 0;
    std::string target;
    std::string state;      // queued, running, done, failed, cancelled
    std::vector<StepView> steps;
    int at = -1;            // the step running, an index into steps
    int n = 0, of = 0;      // its bar, when it has one
    std::string label;      // what the bar is on: a sheet, an image
    std::string line;       // the step's latest word
    std::string error;
    double started = 0.0, ended = 0.0;
    int cores = 0;          // the cores it cooks with, once it starts; 0 not yet known
    std::vector<int> planIndex;  // the plan's step number -> an index into steps, or -1

    // Seconds left, as the server reckons it: the estimates of the steps to come, and the
    // running step's bar once it moves. -1 when a step to come was never timed.
    double eta(double now) const;
    bool finished() const { return state == "done" || state == "failed" || state == "cancelled"; }
};

// What a make of a thing would run, the stale marker: every step's need and reason.
struct PlanView {
    std::string target;
    bool answered = false;
    std::string error;      // no recipe, or no server
    struct Step {
        std::string name;
        bool needed = false;
        std::string reason;
    };
    std::vector<Step> steps;
    bool stale() const;
};

// A recipe as the inspector edits it (tools/mu/edit.py's `recipe`): each of its files --
// one for a weapon, the pieces for an armour set, the sheet itself for the light -- and the
// material library an island may wear.
struct RecipeView {
    std::string target;
    bool answered = false;
    std::string error;
    struct File {
        std::string path;   // relative to MU2_BGFX
        core::Json doc;
    };
    std::vector<File> files;
    std::vector<std::string> materials;
};

class Cooker {
public:
    ~Cooker();

    // `root` is the MU2_BGFX folder: the socket, ./mu and the logs are under it.
    void open(const std::string& root);
    void close();

    // Once a frame: (re)connects every two seconds while there is no server, reads what the
    // stream has brought, and reaps a server this studio started if it ended.
    void poll();
    bool connected() const { return stream_ >= 0; }
    // Starts `./mu serve`, detached. False when it could not be started.
    bool startServer();
    bool started() const { return serverPid_ > 0; }

    // A job for `mu make <target>`; its number, or 0 with the reason in lastError().
    int make(const std::string& target);
    bool cancel(int job, bool now);
    const std::string& lastError() const { return error_; }

    // Asks what a make of `target` would run, on a thread; plan() is the last answer, or the
    // one still being asked (answered false).
    void askPlan(const std::string& target);
    PlanView plan() const;
    // The next askPlan asks again, even for the same target: a job has finished since.
    void forgetPlan();

    // The inspector's two. askRecipe reads on a thread, as askPlan does, and is asked again
    // after forgetRecipe; edit sets one scalar and waits for the answer, which is quick: the
    // file is written and the journal's line comes back, or the refusal with its reason.
    // `path` and `value` are JSON: ["islands", 0, "material"] and "\"brass\"".
    void askRecipe(const std::string& target);
    RecipeView recipe() const;
    void forgetRecipe();
    // Another hand edited a source (the journal, StudioDesk::watchJournal): the inspector's own
    // copy of the recipe is read again on its next draw. Taken once.
    void recipeEdited() { recipeEdited_ = true; }
    bool takeRecipeEdited() { return std::exchange(recipeEdited_, false); }
    // `how` is "set", or tools/mu/edit.py's "insert" (value put in at path: a list's index, or
    // a block's new key) or "remove" (path taken out; value unused).
    bool edit(const std::string& file, const std::string& path, const std::string& value,
              std::string* answer, const char* how = "set");
    // A gate with its pair (tools/mu/gates.py): `fields` are the request's own, JSON members --
    // "how": "add", "world", "to", "column", "row" or "how": "remove", "world", "number". The
    // answer says what was written; `number` is the new way in's, or 0.
    bool gate(const std::string& fields, std::string* answer, int* number = nullptr);
    // The ground painted (tools/mu/paint.py): "world", "tiles": [[col, row, l1, l2, a]], or
    // "how": "undo", "world". The answer says how many tiles changed.
    bool paint(const std::string& fields, std::string* answer);

    const std::vector<JobView>& jobs() const { return jobs_; }
    // The cores a cook is held to (tools/mu/settings.py): the count, the machine's, and where
    // the count came from ("60% of this machine's 10", MU_CORES, the settings file). As mu
    // serve last said it: on connecting, and again as each job starts. Count 0 when unknown.
    struct Cores {
        int count = 0, machine = 0;
        std::string from;
    };
    const Cores& cores() const { return cores_; }

    // One event off the stream, or a job's view out of a `jobs` reply. Public for the test.
    void take(const core::Json& event);
    void load(const core::Json& view);

private:
    bool request(const std::string& line, std::string* reply);
    bool connect();
    JobView* job(int number);

    std::string root_, socket_;
    int stream_ = -1;
    std::string buffer_;
    double nextTry_ = 0.0, nextStart_ = 0.0;
    bool wasConnected_ = false;
    int serverPid_ = 0;
    std::vector<JobView> jobs_;
    Cores cores_;
    bool recipeEdited_ = false;
    std::string error_;

    mutable std::mutex planLock_;
    PlanView plan_;
    std::unique_ptr<std::thread> planThread_;
    RecipeView recipe_;
    std::unique_ptr<std::thread> recipeThread_;
};

}  // namespace mu::studio
