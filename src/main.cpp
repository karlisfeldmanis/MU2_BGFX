// MU2 on bgfx. The entry point, and deliberately nothing else.
//
// This file was 1663 lines: argument dispatch, a save reader, a preloader with its own thread
// and its spinner, a model-browser list widget, three frame loops wearing one `if` ladder, and
// a teardown. It is `app/` now -- docs/architecture.md has the shape, and app/application.h
// states the division of labour between the Application and a Mode.
//
// See PLAN.md and docs/sprints/ for what any of it is for.
#include "app/application.h"

int main(int argc, char** argv) {
    mu::app::Application application;
    return application.run(argc, argv);
}
