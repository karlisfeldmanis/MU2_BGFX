// The studio's half with no window (docs/order-plan.md §9c, F2): the cook's client and the
// panels, without bgfx and without a display.
//
//   cooker_test          mu serve's events fed by hand: the bar, the step map, the time left,
//                        a view loaded mid-cook; then the panels drawn by ImGui into nothing
//   cooker_test --live   and then a rehearsal through a running `./mu serve`, watched to its
//                        end the way the studio watches it (start one first)
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "core/json.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "studio/cooker.h"
#include "studio/panels.h"

using mu::core::parseJson;
using mu::studio::Cooker;
using mu::studio::JobView;

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const char* what) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what);
    }
}

void feed(Cooker& cooker, const char* text) {
    cooker.take(parseJson(text, std::strlen(text), "a test event"));
}

void events() {
    Cooker cooker;
    feed(cooker, R"({"seq":1,"at":100.0,"job":7,"kind":"queued","target":["Sword01"]})");
    check(cooker.jobs().size() == 1 && cooker.jobs()[0].state == "queued", "queued makes a job");
    check(cooker.jobs()[0].target == "Sword01", "the target is the make's words");

    // Plan step 1 (the checkpoint) does not run, so the bar's steps are plan 0, 2 and 3.
    feed(cooker, R"({"seq":2,"at":101.0,"job":7,"kind":"plan","steps":[
        {"name":"build Sword01","index":0,"state":"waiting","estimate":60.0,"seconds":null},
        {"name":"index","index":2,"state":"waiting","estimate":10.0,"seconds":null},
        {"name":"cook_one Sword01","index":3,"state":"waiting","estimate":30.0,"seconds":null}]})");
    const JobView& job = cooker.jobs()[0];
    check(job.state == "running" && job.steps.size() == 3, "the plan is the job's bar");
    check(job.planIndex.size() == 4 && job.planIndex[2] == 1 && job.planIndex[1] == -1,
          "plan steps map onto the bar's");
    check(std::fabs(job.eta(101.0) - 100.0) < 1e-6, "before it starts, the estimates add up");

    feed(cooker, R"({"seq":3,"at":101.0,"job":7,"kind":"step","step":0,"name":"build Sword01"})");
    feed(cooker, R"({"seq":4,"at":111.0,"job":7,"kind":"progress","step":0,"n":1,"of":4,"label":"sheet sword02.png"})");
    check(job.at == 0 && job.n == 1 && job.of == 4 && job.label == "sheet sword02.png",
          "a step's bar and what it is on");
    // A quarter done in ten seconds is forty for the step, thirty left of it, and forty after.
    check(std::fabs(job.eta(111.0) - 70.0) < 1e-6, "the bar beats the history once it moves");
    feed(cooker, R"({"seq":5,"at":112.0,"job":7,"kind":"line","step":0,"text":"upscaling sword02.png"})");
    check(job.line == "upscaling sword02.png", "the step's latest word");
    feed(cooker, R"({"seq":6,"at":141.0,"job":7,"kind":"done","step":0,"seconds":40.0,"code":0})");
    check(job.steps[0].state == "done" && job.steps[0].seconds == 40.0, "a step done");

    feed(cooker, R"({"seq":7,"at":141.0,"job":7,"kind":"skip","step":1,"name":"checkpoint mu.db"})");
    feed(cooker, R"({"seq":8,"at":141.0,"job":7,"kind":"step","step":2,"name":"index"})");
    check(job.at == 1 && job.steps[1].state == "running", "plan step 2 is the bar's second");
    feed(cooker, R"({"seq":9,"at":143.0,"job":7,"kind":"done","step":2,"seconds":2.0,"code":1})");
    feed(cooker, R"({"seq":10,"at":143.0,"job":7,"kind":"finished","ok":false,"state":"failed","error":"index failed with 1"})");
    check(job.state == "failed" && job.error == "index failed with 1", "a failed job says why");
    check(job.steps[1].state == "failed" && job.steps[2].state == "not run",
          "the steps after a failure did not run");
    check(job.eta(143.0) == 0.0 && job.finished(), "a finished job has no time left");

    feed(cooker, R"({"seq":11,"at":1.0,"job":99,"kind":"progress","n":1,"of":2})");
    check(cooker.jobs().size() == 1, "an event for a job never queued is dropped");

    // A studio opened mid-cook reads the job's view out of `jobs`, then the events after it.
    const char* view = R"({"job":8,"target":"world lorencia","state":"running","at":1,
        "steps":[{"name":"textures lorencia","index":0,"state":"done","estimate":20,"seconds":21},
                 {"name":"meshes lorencia","index":1,"state":"running","estimate":8,"began":200}],
        "progress":{"n":3,"of":6,"label":"compress"},"line":"","error":null,"started":179,"ended":null})";
    cooker.load(parseJson(view, std::strlen(view), "a job view"));
    const JobView& later = cooker.jobs().back();
    check(later.number == 8 && later.at == 1 && later.n == 3 && later.of == 6, "a view loaded mid-cook");
    feed(cooker, R"({"seq":20,"at":205.0,"job":8,"kind":"done","step":1,"seconds":5.0,"code":0})");
    check(later.steps[1].state == "done", "its events go on from there");
}

void panels() {
    // ImGui with no renderer: the panels laid out and tessellated, nothing drawn.
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1920.0f, 1080.0f);
    // As gfx::Gui declares: the atlas is grown on demand and handed to the renderer as a texture
    // request, which nothing here answers. Without the flag ImGui wants an atlas built up front.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
    io.Fonts->AddFontDefault();

    Cooker cooker;  // never opened: no server, so the panels show that
    feed(cooker, R"({"seq":1,"at":100.0,"job":1,"kind":"queued","target":["rehearsal"]})");
    feed(cooker, R"({"seq":2,"at":100.0,"job":1,"kind":"plan","steps":[
        {"name":"rehearse sheets","index":0,"state":"waiting","estimate":2.1}]})");
    feed(cooker, R"({"seq":3,"at":100.0,"job":1,"kind":"step","step":0,"name":"rehearse sheets"})");
    feed(cooker, R"({"seq":4,"at":101.0,"job":1,"kind":"progress","step":0,"n":3,"of":8,"label":"sheet 4"})");

    mu::studio::Shelf shelf;
    shelf.categories = {"World objects", "Monsters", "Weapons"};
    shelf.category = 2;
    shelf.names = {"Kris", "Short Sword", "Rapier"};
    shelf.selected = 1;
    shelf.target = "Sword01";

    mu::studio::Panels panels;
    mu::studio::Asked asked;
    int vertices = 0;
    for (int frame = 0; frame < 3; ++frame) {
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        asked = panels.draw(shelf, cooker, 1920.0f, 1080.0f, 101.0);
        ImGui::Render();
        vertices = ImGui::GetDrawData()->TotalVtxCount;
    }
    std::printf("panels: %d vertices\n", vertices);
    check(vertices > 500, "the panels tessellate");
    check(asked.category == -1 && asked.pick == -1, "no click, nothing asked");
    for (const char* name : {"Browser", "Subject", "Jobs"}) {
        ImGuiWindow* window = ImGui::FindWindowByName(name);
        check(window && window->Active, name);
        if (window) {
            const bool inside = window->Pos.x >= 0.0f && window->Pos.y >= 0.0f &&
                                window->Pos.x + window->Size.x <= 1920.0f + 0.5f &&
                                window->Pos.y + window->Size.y <= 1080.0f + 0.5f;
            check(inside, "a panel stays on the screen");
        }
    }
    ImGuiWindow* browser = ImGui::FindWindowByName("Browser");
    ImGuiWindow* jobs = ImGui::FindWindowByName("Jobs");
    if (browser && jobs) {
        check(browser->Pos.y + browser->Size.y <= jobs->Pos.y, "the browser ends above the jobs");
    }

    // A click on a row asks for it: the pointer put on the third name, pressed and let go.
    ImGuiWindow* names = nullptr;
    for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows) {
        if (std::strstr(w->Name, "Browser/names")) names = w;
    }
    check(names != nullptr, "the browser's list");
    if (names) {
        const float row = ImGui::GetTextLineHeightWithSpacing();
        const ImVec2 at(names->Pos.x + 20.0f, names->Pos.y + row * 2.5f);
        for (int frame = 0; frame < 4; ++frame) {
            io.AddMousePosEvent(at.x, at.y);
            io.AddMouseButtonEvent(0, frame == 1);
            ImGui::NewFrame();
            const mu::studio::Asked a = panels.draw(shelf, cooker, 1920.0f, 1080.0f, 101.0);
            if (a.pick >= 0) asked = a;
            ImGui::Render();
        }
        check(asked.pick == 2, "a click on the third name picks it");
    }
    ImGui::DestroyContext();
}

void live() {
    Cooker cooker;
    cooker.open(MU2_ROOT_DIR);
    for (int i = 0; i < 20 && !cooker.connected(); ++i) {
        cooker.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    check(cooker.connected(), "live: connected to mu serve (start it: ./mu serve)");
    if (!cooker.connected()) return;

    cooker.askPlan("rehearsal");
    const int number = cooker.make("rehearsal");
    check(number > 0, "live: a rehearsal queued");
    int bars = 0, lastN = -1;
    const JobView* job = nullptr;
    for (int i = 0; i < 400; ++i) {
        cooker.poll();
        for (const JobView& j : cooker.jobs()) {
            if (j.number == number) job = &j;
        }
        if (job && job->n != lastN && job->of > 0) {
            ++bars;
            lastN = job->n;
        }
        if (job && job->finished()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    check(job && job->state == "done", "live: the rehearsal finished");
    check(job && job->steps.size() == 3, "live: three steps on its bar");
    check(bars >= 10, "live: its bars were seen moving");
    const mu::studio::PlanView plan = cooker.plan();
    check(plan.answered && plan.error.empty() && plan.steps.size() == 3 && plan.stale(),
          "live: a plan asked and answered");
    cooker.forgetPlan();
    cooker.askPlan("NoSuchThing99");
    for (int i = 0; i < 100 && !cooker.plan().answered; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    check(cooker.plan().error.find("no recipe") != std::string::npos, "live: an unknown name's plan says so");
    std::printf("live: job %d, %d bar moves seen\n", number, bars);
    cooker.close();
}

}  // namespace

int main(int argc, char** argv) {
    events();
    panels();
    if (argc > 1 && std::strcmp(argv[1], "--live") == 0) live();
    std::printf("cooker_test: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
