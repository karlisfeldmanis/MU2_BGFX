// The pictures' frame buffers without a GPU: bgfx's Noop renderer draws nothing and keeps the
// real handle pools, so what a portrait, an item stage and the desk take of its 128 frame
// buffers can be counted.
//
// GeraColorz in Discord, 2026-10-24: the vault opened as a black panel of items in the screen's
// top-left corner. The desk is shut at every gate, warp and trip to the character screen and
// let the target's face and the party's keep their buffers; the pool ran out, the vault stage's
// buffer was refused, and bgfx drew an invalid buffer's view on the backbuffer.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <bgfx/bgfx.h>

#include "game/portrait.h"
#include "game/ui/desk.h"
#include "game/ui/items_stage.h"
#include "gfx/renderer.h"
#include "gfx/views.h"

using namespace mu;
using namespace mu::game;

namespace {

int g_checks = 0, g_failures = 0;

void check(bool held, const std::string& what) {
    ++g_checks;
    if (held) return;
    ++g_failures;
    std::printf("FAIL: %s\n", what.c_str());
}

void checkEqual(long long got, long long wanted, const std::string& what) {
    ++g_checks;
    if (got == wanted) return;
    ++g_failures;
    std::printf("FAIL: %s (got %lld, wanted %lld)\n", what.c_str(), got, wanted);
}

// A destroyed handle goes back to its pool at the next frame, so a count is taken after one.
int frameBuffers() {
    bgfx::frame();
    return bgfx::getStats()->numFrameBuffers;
}
int textures() {
    bgfx::frame();
    return bgfx::getStats()->numTextures;
}

// One drawable is enough to be photographed: the Noop renderer has no stage program, so
// drawStage points the view and returns before the mesh is read.
std::vector<gfx::Drawable> someone() { return std::vector<gfx::Drawable>(1); }
const float kHead[3] = {0.0f, 1.6f, 0.0f};

std::vector<Standing> standing(int item) {
    Standing one;
    one.item = item;
    return {one};
}

}  // namespace

namespace mu::game {
// Desk's friend: its faces, as Desk::photograph fills them from a Play.
struct DeskFaces {
    static void take(Desk& desk, gfx::Renderer& renderer, int party) {
        desk.portrait_.render(renderer, someone(), kHead, 0.0f, 1.0f);
        for (int i = 0; i < party; ++i) {
            desk.partyFaces_["member" + std::to_string(i)].render(renderer, someone(), kHead, 0.0f, 1.0f);
        }
    }
};
}  // namespace mu::game

namespace {

void testPortrait(gfx::Renderer& renderer) {
    const int before = frameBuffers();
    Portrait face;
    face.render(renderer, someone(), kHead, 0.0f, 1.0f);
    check(face.taken(), "a portrait photographed is taken");
    check(face.picture().valid(), "a portrait photographed has a picture");
    checkEqual(frameBuffers(), before + 1, "a portrait takes one frame buffer");
    face.render(renderer, someone(), kHead, 0.0f, 1.0f);
    checkEqual(frameBuffers(), before + 1, "photographed again, it keeps the one it has");
    face.shutdown();
    checkEqual(frameBuffers(), before, "a portrait shut gives its frame buffer back");
}

void testItemStage(gfx::Renderer& renderer) {
    const int before = frameBuffers();
    ItemStage stage;
    stage.open(nullptr, gfx::ViewStageTip);
    stage.stand(standing(0), 20.0f, 20.0f);
    stage.render(renderer, 2.0f, 0.0);
    check(stage.picture().valid(), "a stage rendered has a picture");
    checkEqual(frameBuffers(), before + 1, "a stage takes one frame buffer");
    // A tooltip's stage takes a new size for every item it shows; the old ones are kept for
    // the windows that still hold their pictures, a few and no more.
    for (int i = 0; i < 40; ++i) {
        stage.stand(standing(i), 20.0f + float(i % 9) * 10.0f, 20.0f + float(i % 5) * 10.0f);
        stage.render(renderer, 2.0f, 0.0);
    }
    check(frameBuffers() <= before + 4, "forty sizes keep at most four frame buffers");
    stage.shutdown();
    checkEqual(frameBuffers(), before, "a stage shut gives every frame buffer back");
}

// The regression: a desk opened for every world he is handed to, his target framed and his
// party's faces on their capsules, and shut when the world hands him on. Thirty gates.
void testDeskTravels(gfx::Renderer& renderer) {
    const int before = frameBuffers();
    for (int travel = 0; travel < 30; ++travel) {
        auto desk = std::make_unique<Desk>();
        DeskFaces::take(*desk, renderer, 4);
        if (travel == 0) checkEqual(frameBuffers(), before + 5, "the target's face and four of the party's");
        desk->shutdown();
        desk.reset();
    }
    checkEqual(frameBuffers(), before, "thirty gates with a party leave no frame buffer behind");
}

// The pool run dry: nothing is drawn through an invalid buffer -- which bgfx takes for the
// backbuffer -- and the textures made for a refused one go back, every frame it is asked again.
void testRefused(gfx::Renderer& renderer) {
    std::vector<bgfx::FrameBufferHandle> filler;
    for (;;) {
        const bgfx::FrameBufferHandle fb =
            bgfx::createFrameBuffer(4, 4, bgfx::TextureFormat::RGBA8);
        if (!bgfx::isValid(fb)) break;
        filler.push_back(fb);
    }
    check(!filler.empty(), "the pool was filled");
    const int texturesDry = textures();

    Portrait face;
    for (int frame = 0; frame < 5; ++frame) face.render(renderer, someone(), kHead, 0.0f, 1.0f);
    check(!face.taken(), "a portrait refused its buffer is not taken");
    check(!face.picture().valid(), "a portrait refused its buffer has no picture to draw");

    ItemStage stage;
    stage.open(nullptr, gfx::ViewStageTip);
    stage.stand(standing(0), 20.0f, 20.0f);
    for (int frame = 0; frame < 5; ++frame) stage.render(renderer, 2.0f, 0.0);
    check(!stage.picture().valid(), "a stage refused its buffer has no picture to draw");
    checkEqual(textures(), texturesDry, "refused five frames running, no texture is left behind");

    // And with room again, both take theirs.
    for (const bgfx::FrameBufferHandle fb : filler) bgfx::destroy(fb);
    filler.clear();
    bgfx::frame();
    face.render(renderer, someone(), kHead, 0.0f, 1.0f);
    stage.render(renderer, 2.0f, 0.0);
    check(face.taken(), "with room again the portrait is taken");
    check(stage.picture().valid(), "with room again the stage has its picture");
    face.shutdown();
    stage.shutdown();
}

}  // namespace

int main() {
    bgfx::Init init;
    init.type = bgfx::RendererType::Noop;
    if (!bgfx::init(init)) {
        std::printf("FAIL: bgfx's Noop renderer did not start\n");
        return 1;
    }
    {
        gfx::Renderer renderer;
        testPortrait(renderer);
        testItemStage(renderer);
        testDeskTravels(renderer);
        testRefused(renderer);
    }
    bgfx::shutdown();
    std::printf("stage_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
