// The studio's desk: the panels over the bench in `mu2 --studio` (docs/order-plan.md §9c, F2).
//
// The bench mode's Studio kind owns one and calls it at four points of its frame: begin before
// its steering, panels where the overlay's list was drawn, end after the world, and wantsPointer
// to decide whether a drag orbits the camera. What it shows of the bench is the bench's own
// browser -- tabs, names, the one standing there -- and each name's `mu make` target; what it
// cooks goes through `mu serve`, which it starts when none is running.
//
// Off under --no-list, as the overlay's list was, so tools/studio.py's sweeps (which pass it)
// shoot the subject with nothing over it.
//
// **The swap** (F3): when a job that cooked the subject finishes, the subject is held still --
// its clip from its first frame, the camera's turn stopped -- the panels are hidden, and a
// shot is taken; the bench reads its tables and textures again (ModelBench::reopen), the
// same pose is shot again, and `mu compare` lays the two side by side with how much moved,
// in shots/studio/swaps/<name>_<when>/. Eight frames, and the new look is on the bench.
// The card's Read again runs the same swap with no job, for a cook run by hand.
//
// **The control socket** (F7): workshop/.mu/studio.sock takes the same lines one at a time and
// answers each -- `./mu do "subject Weapons|Kris; scene Home; wait 30; shot /abs/x.png"` -- so
// an agent drives a studio the user opened, or one it opened for a shot run, and reads back
// what it asked for.
//
// MU_STUDIO_DO="<frame>:<what>;..." does what the card would on those frames, for a shot run
// with nobody at the mouse: `swap` (Read again), `scene <Stage|Home|world>`, `plus <N>`, `tab light` and `arena` -- until F7's
// control socket drives the studio (docs/order-plan.md §9c). "40:scene Home;200:swap".
#pragma once

#include <string>
#include <vector>

#include "app/modes/studio_marks.h"
#include "app/modes/studio_place.h"
#include "gfx/gui.h"
#include "studio/cooker.h"
#include "studio/panels.h"

namespace mu::game {
class ModelBench;
class World;
}

namespace mu::app {

struct Context;

class StudioDesk {
public:
    bool open(Context& ctx);
    bool isOpen() const { return gui_.isOpen(); }
    void begin(Context& ctx, float deltaSeconds, double elapsed);
    // `world` is the bench's, which a swap reopens the tables of.
    // `town` and `camera` are the scene's, for Place; `onStage` when no town is drawn.
    void panels(Context& ctx, game::ModelBench& bench, const std::string& world,
                game::World& town, const gfx::Camera& camera, bool onStage);
    // Whether Place had the pointer this frame (a pick or a walk), so the camera keeps still.
    bool tookPointer() const { return tookPointer_; }
    // `quit` on the control socket: the studio closes as Escape would.
    bool quitAsked() const { return quit_; }
    void end();
    // While a swap is shooting: the bench is to be updated at heldSeconds() with no delta.
    bool holding() const { return swap_ != Swap::Idle; }
    double heldSeconds() const { return heldSeconds_; }
    // The scene the card asked for this frame ("Stage", "Home" or a world), once; empty if none.
    std::string takeScene();
    // The refinement the inspector asked for this frame, once; -1 if none.
    int takePlus() { const int p = plusAsked_; plusAsked_ = -1; return p; }
    bool wantsPointer() const { return gui_.wantsPointer(); }
    bool wantsKeys() const { return gui_.wantsKeys(); }
    void shutdown();

    // What `mu make` calls the thing on the bench: world/Name for a world object, the
    // figure's own name for a body; empty when the entry is neither.
    static std::string targetOf(const game::ModelBench& bench);

private:
    void watchJobs(game::ModelBench& bench);
    // One thing the card would do, by name: MU_STUDIO_DO's lines and the control socket's
    // (app/modes/studio_control.cpp). The answer is one line: "ok ..." or "error ...".
    std::string act(Context& ctx, game::ModelBench& bench, const std::string& world,
                    game::World& town, const std::string& what);
    // The control socket (F7): workshop/.mu/studio.sock, a line in and a line out.
    void openControl(Context& ctx);
    void serveControl(Context& ctx, game::ModelBench& bench, const std::string& world,
                      game::World& town);
    void closeControl();
    // The arena window (F5): `mu2 --arena <breed> --world <world>`, a process of its own.
    void openArena(const std::string& breed, const std::string& world);
    void closeArena();
    bool arenaUp();
    void startSwap(game::ModelBench& bench, const std::string& name);
    void stepSwap(Context& ctx, game::ModelBench& bench, const std::string& world);

    gfx::Gui gui_;
    studio::Cooker cooker_;
    studio::Panels panels_;
    studio::Shelf shelf_;
    int shelfCategory_ = -1;   // the tab shelf_.names was read for

    enum class Swap { Idle, Before, After };
    Swap swap_ = Swap::Idle;
    int swapFrames_ = 0;
    double elapsed_ = 0.0, heldSeconds_ = 0.0;
    std::string swapDir_;
    std::vector<int> seen_;    // finished jobs already answered, or there before the studio was
    bool seenAll_ = false;
    std::string shelfRoot_;   // MU2_BGFX: ./mu and shots/ are under it
    std::string lastSwap_;    // the last swap's folder, for the card
    int comparePid_ = 0;
    int arenaPid_ = 0;
    std::string arenaBreed_, arenaWorld_;
    int frame_ = 0;   // this desk's frames
    std::vector<std::pair<int, std::string>> script_;   // MU_STUDIO_DO
    std::string sceneAsked_;
    int plusAsked_ = -1;
    StudioPlace place_;
    StudioMarks marks_;   // gates and townsfolk, sprint G
    int placeMode_ = 0;   // placeModeBar's: 0 objects, 1 gates, 2 townsfolk
    bool placing_ = false, tookPointer_ = false;
    // The control socket's listener and its clients, each with the frame its next line waits for.
    int listener_ = -1;
    std::string controlPath_;
    struct Client {
        int fd = -1;
        std::string in;
        int waitUntil = 0;
    };
    std::vector<Client> clients_;
    bool quit_ = false;
    bool hidden_ = false;   // `hide`: the panels left undrawn, for a clean shot
};

}  // namespace mu::app
