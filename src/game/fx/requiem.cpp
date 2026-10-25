#include "game/fx/requiem.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

// Neil's blade, baked by muextract export-rig on Data/Skill/summon_neil.bmd and composed MU's way
// (positions lerped, rotations slerped between keys, down the skeleton), in the effect corner frame
// (MU X, MU Z, MU Y), in MU units, about her feet.
// Bones 51 and 59 every tenth of a key, 0-12.
constexpr float kBladeEnds[121][2][3] = {
    {{-37.1f, 84.0f, 27.3f}, {-102.6f, 10.4f, -52.0f}},
    {{-36.1f, 81.0f, 27.5f}, {-101.8f, 9.3f, -53.5f}},
    {{-35.4f, 78.0f, 27.7f}, {-101.5f, 8.4f, -54.8f}},
    {{-34.9f, 75.1f, 27.8f}, {-101.6f, 7.9f, -56.1f}},
    {{-34.6f, 72.4f, 27.8f}, {-102.0f, 7.5f, -57.4f}},
    {{-34.5f, 69.8f, 27.8f}, {-102.6f, 7.4f, -58.7f}},
    {{-34.5f, 67.2f, 27.8f}, {-103.2f, 7.5f, -60.1f}},
    {{-34.7f, 64.7f, 27.6f}, {-103.8f, 7.6f, -61.7f}},
    {{-35.1f, 62.3f, 27.4f}, {-104.1f, 7.7f, -63.5f}},
    {{-35.5f, 60.0f, 27.0f}, {-104.1f, 7.8f, -65.6f}},
    {{-36.0f, 57.6f, 26.6f}, {-103.5f, 7.6f, -68.0f}},
    {{-35.8f, 57.8f, 28.1f}, {-105.4f, 7.6f, -64.9f}},
    {{-35.6f, 58.1f, 29.5f}, {-107.2f, 7.7f, -61.9f}},
    {{-35.4f, 58.4f, 30.7f}, {-108.8f, 7.8f, -59.1f}},
    {{-35.1f, 58.7f, 31.8f}, {-110.2f, 8.0f, -56.5f}},
    {{-34.8f, 59.1f, 32.8f}, {-111.5f, 8.1f, -54.0f}},
    {{-34.4f, 59.5f, 33.6f}, {-112.6f, 8.2f, -51.7f}},
    {{-34.1f, 59.8f, 34.3f}, {-113.6f, 8.3f, -49.5f}},
    {{-33.7f, 60.2f, 34.8f}, {-114.5f, 8.4f, -47.6f}},
    {{-33.3f, 60.5f, 35.2f}, {-115.3f, 8.4f, -45.9f}},
    {{-32.9f, 60.9f, 35.5f}, {-115.9f, 8.3f, -44.3f}},
    {{-33.1f, 61.7f, 34.9f}, {-114.4f, 7.9f, -45.8f}},
    {{-33.2f, 62.6f, 34.2f}, {-112.8f, 7.5f, -47.2f}},
    {{-33.3f, 63.5f, 33.6f}, {-111.2f, 7.1f, -48.7f}},
    {{-33.3f, 64.4f, 32.9f}, {-109.5f, 6.7f, -50.0f}},
    {{-33.4f, 65.2f, 32.3f}, {-107.9f, 6.3f, -51.3f}},
    {{-33.4f, 66.1f, 31.7f}, {-106.1f, 5.8f, -52.5f}},
    {{-33.5f, 67.0f, 31.0f}, {-104.4f, 5.4f, -53.7f}},
    {{-33.5f, 68.0f, 30.4f}, {-102.6f, 5.0f, -54.8f}},
    {{-33.5f, 68.9f, 29.8f}, {-100.8f, 4.6f, -55.9f}},
    {{-33.4f, 69.8f, 29.2f}, {-99.0f, 4.2f, -56.9f}},
    {{-38.9f, 69.7f, 15.9f}, {-86.5f, 26.0f, -92.9f}},
    {{-41.5f, 72.8f, 0.4f}, {-68.1f, 55.5f, -122.1f}},
    {{-41.0f, 79.7f, -15.9f}, {-45.5f, 91.1f, -141.9f}},
    {{-37.4f, 90.5f, -31.5f}, {-20.8f, 130.7f, -150.3f}},
    {{-31.0f, 105.1f, -44.8f}, {3.9f, 171.7f, -146.6f}},
    {{-22.5f, 122.9f, -54.3f}, {26.2f, 211.3f, -130.7f}},
    {{-12.8f, 142.9f, -59.1f}, {43.9f, 246.8f, -103.8f}},
    {{-3.0f, 163.9f, -58.5f}, {55.2f, 275.9f, -68.0f}},
    {{5.7f, 184.7f, -52.5f}, {59.0f, 296.5f, -26.2f}},
    {{12.3f, 204.0f, -41.4f}, {54.6f, 307.4f, 18.1f}},
    {{11.0f, 216.6f, -38.1f}, {33.7f, 321.5f, 28.8f}},
    {{8.7f, 229.0f, -34.9f}, {10.6f, 333.5f, 36.4f}},
    {{5.7f, 240.9f, -31.7f}, {-14.0f, 342.8f, 40.7f}},
    {{2.1f, 252.3f, -28.5f}, {-39.4f, 349.2f, 41.6f}},
    {{-2.2f, 263.0f, -25.5f}, {-64.6f, 352.2f, 39.0f}},
    {{-6.7f, 272.7f, -22.7f}, {-88.6f, 351.8f, 32.8f}},
    {{-11.5f, 281.5f, -20.2f}, {-110.1f, 348.2f, 22.8f}},
    {{-16.1f, 289.3f, -18.2f}, {-128.0f, 341.8f, 9.0f}},
    {{-20.4f, 296.0f, -16.6f}, {-141.0f, 333.5f, -8.2f}},
    {{-24.1f, 301.8f, -15.7f}, {-148.0f, 324.2f, -28.4f}},
    {{-22.6f, 310.9f, -16.6f}, {-141.0f, 344.0f, -46.6f}},
    {{-20.6f, 319.8f, -17.1f}, {-130.3f, 363.1f, -62.9f}},
    {{-17.9f, 328.4f, -17.1f}, {-116.2f, 381.0f, -77.0f}},
    {{-14.9f, 336.5f, -16.6f}, {-99.5f, 397.2f, -88.6f}},
    {{-11.6f, 344.1f, -15.6f}, {-80.7f, 411.2f, -97.6f}},
    {{-8.0f, 351.0f, -14.0f}, {-60.4f, 422.8f, -104.1f}},
    {{-4.4f, 357.3f, -11.8f}, {-39.4f, 431.7f, -108.0f}},
    {{-0.9f, 362.7f, -9.1f}, {-18.0f, 437.8f, -109.5f}},
    {{2.4f, 367.4f, -5.9f}, {3.0f, 441.1f, -108.7f}},
    {{5.5f, 371.2f, -2.2f}, {23.2f, 441.7f, -105.8f}},
    {{5.8f, 376.0f, 1.0f}, {25.7f, 452.5f, -97.8f}},
    {{5.9f, 380.6f, 4.3f}, {28.0f, 462.9f, -89.3f}},
    {{6.1f, 385.2f, 7.7f}, {30.1f, 472.9f, -80.3f}},
    {{6.1f, 389.6f, 11.2f}, {31.9f, 482.5f, -70.8f}},
    {{6.1f, 393.8f, 14.7f}, {33.5f, 491.5f, -60.9f}},
    {{6.0f, 398.0f, 18.4f}, {34.9f, 500.0f, -50.6f}},
    {{5.8f, 401.9f, 22.2f}, {36.1f, 508.0f, -39.9f}},
    {{5.6f, 405.7f, 26.0f}, {36.9f, 515.4f, -28.8f}},
    {{5.3f, 409.3f, 29.9f}, {37.5f, 522.2f, -17.5f}},
    {{4.9f, 412.8f, 33.8f}, {37.9f, 528.3f, -5.9f}},
    {{4.0f, 412.7f, 33.6f}, {34.0f, 529.4f, -5.0f}},
    {{3.0f, 412.6f, 33.4f}, {30.0f, 530.4f, -4.1f}},
    {{1.9f, 412.5f, 33.2f}, {25.8f, 531.3f, -3.2f}},
    {{0.8f, 412.3f, 33.0f}, {21.6f, 532.1f, -2.3f}},
    {{-0.4f, 412.2f, 32.8f}, {17.1f, 532.7f, -1.5f}},
    {{-1.7f, 412.0f, 32.6f}, {12.6f, 533.2f, -0.8f}},
    {{-3.0f, 411.7f, 32.4f}, {7.9f, 533.5f, -0.0f}},
    {{-4.5f, 411.4f, 32.2f}, {3.2f, 533.8f, 0.7f}},
    {{-5.9f, 411.1f, 32.0f}, {-1.7f, 533.8f, 1.3f}},
    {{-7.5f, 410.8f, 31.7f}, {-6.7f, 533.7f, 1.9f}},
    {{-6.4f, 393.3f, -5.9f}, {-5.7f, 500.2f, -73.7f}},
    {{-5.3f, 365.8f, -43.3f}, {-4.4f, 445.9f, -141.4f}},
    {{-4.2f, 328.9f, -77.5f}, {-2.8f, 374.5f, -195.6f}},
    {{-3.1f, 283.9f, -106.0f}, {-1.3f, 291.3f, -232.4f}},
    {{-2.3f, 233.0f, -126.9f}, {-0.1f, 202.1f, -249.6f}},
    {{-1.7f, 178.7f, -139.0f}, {0.8f, 112.9f, -247.0f}},
    {{-1.3f, 123.6f, -141.8f}, {1.2f, 29.0f, -225.8f}},
    {{-1.4f, 70.2f, -135.6f}, {1.1f, -44.6f, -188.8f}},
    {{-1.7f, 21.0f, -121.5f}, {0.4f, -104.3f, -139.6f}},
    {{-2.4f, -22.2f, -100.9f}, {-0.6f, -147.4f, -82.6f}},
    {{-2.4f, -22.2f, -100.9f}, {-0.7f, -147.4f, -82.7f}},
    {{-2.5f, -22.1f, -101.0f}, {-0.7f, -147.4f, -82.8f}},
    {{-2.5f, -22.1f, -101.0f}, {-0.8f, -147.4f, -82.9f}},
    {{-2.5f, -22.1f, -101.1f}, {-0.8f, -147.4f, -82.9f}},
    {{-2.5f, -22.1f, -101.1f}, {-0.9f, -147.4f, -82.9f}},
    {{-2.5f, -22.1f, -101.1f}, {-0.9f, -147.4f, -82.9f}},
    {{-2.5f, -22.1f, -101.1f}, {-1.0f, -147.4f, -82.9f}},
    {{-2.6f, -22.1f, -101.0f}, {-1.0f, -147.4f, -82.9f}},
    {{-2.6f, -22.2f, -101.0f}, {-1.1f, -147.4f, -82.9f}},
    {{-2.6f, -22.2f, -100.9f}, {-1.1f, -147.4f, -82.8f}},
    {{-2.5f, -22.2f, -100.9f}, {-0.9f, -147.4f, -82.8f}},
    {{-2.4f, -22.2f, -100.9f}, {-0.6f, -147.4f, -82.7f}},
    {{-2.3f, -22.2f, -100.9f}, {-0.4f, -147.4f, -82.6f}},
    {{-2.2f, -22.2f, -100.8f}, {-0.1f, -147.4f, -82.5f}},
    {{-2.1f, -22.2f, -100.8f}, {0.1f, -147.3f, -82.4f}},
    {{-2.0f, -22.2f, -100.8f}, {0.3f, -147.3f, -82.3f}},
    {{-1.9f, -22.2f, -100.7f}, {0.6f, -147.3f, -82.2f}},
    {{-1.9f, -22.2f, -100.7f}, {0.9f, -147.3f, -82.1f}},
    {{-1.8f, -22.2f, -100.6f}, {1.1f, -147.3f, -81.9f}},
    {{-1.7f, -22.2f, -100.5f}, {1.4f, -147.3f, -81.8f}},
    {{-1.7f, -22.2f, -100.5f}, {1.3f, -147.3f, -81.8f}},
    {{-1.7f, -22.2f, -100.5f}, {1.3f, -147.3f, -81.8f}},
    {{-1.7f, -22.2f, -100.6f}, {1.2f, -147.3f, -81.9f}},
    {{-1.7f, -22.2f, -100.6f}, {1.2f, -147.3f, -81.9f}},
    {{-1.7f, -22.2f, -100.6f}, {1.2f, -147.3f, -81.9f}},
    {{-1.7f, -22.2f, -100.6f}, {1.1f, -147.3f, -81.9f}},
    {{-1.8f, -22.2f, -100.6f}, {1.1f, -147.3f, -81.9f}},
    {{-1.8f, -22.2f, -100.6f}, {1.0f, -147.3f, -81.9f}},
    {{-1.8f, -22.2f, -100.6f}, {1.0f, -147.3f, -82.0f}},
    {{-1.8f, -22.2f, -100.6f}, {1.0f, -147.3f, -82.0f}},
};
// Bones 51-59 at each key.
constexpr float kBladeBones[13][9][3] = {
    {{-37.1f, 84.0f, 27.3f}, {-44.9f, 74.1f, 17.6f}, {-52.7f, 64.3f, 7.9f}, {-60.6f, 54.4f, -1.8f}, {-68.4f, 44.5f, -11.6f}, {-76.2f, 34.6f, -21.3f}, {-84.8f, 25.7f, -31.5f}, {-93.2f, 17.6f, -41.3f}, {-102.6f, 10.4f, -52.0f}},
    {{-36.0f, 57.6f, 26.6f}, {-44.3f, 50.6f, 15.0f}, {-52.7f, 43.6f, 3.3f}, {-61.1f, 36.5f, -8.3f}, {-69.4f, 29.5f, -19.9f}, {-77.8f, 22.5f, -31.5f}, {-86.4f, 16.7f, -43.6f}, {-94.7f, 11.6f, -55.3f}, {-103.5f, 7.6f, -68.0f}},
    {{-32.9f, 60.9f, 35.5f}, {-43.4f, 53.6f, 25.9f}, {-53.8f, 46.3f, 16.3f}, {-64.3f, 39.1f, 6.7f}, {-74.7f, 31.8f, -2.9f}, {-85.2f, 24.5f, -12.5f}, {-95.6f, 18.3f, -22.8f}, {-105.6f, 12.8f, -33.0f}, {-115.9f, 8.3f, -44.3f}},
    {{-33.4f, 69.8f, 29.2f}, {-41.7f, 60.9f, 18.8f}, {-50.0f, 52.1f, 8.5f}, {-58.3f, 43.2f, -1.8f}, {-66.6f, 34.3f, -12.2f}, {-74.8f, 25.5f, -22.5f}, {-83.1f, 17.6f, -33.7f}, {-90.9f, 10.5f, -44.6f}, {-99.0f, 4.2f, -56.9f}},
    {{12.3f, 204.0f, -41.4f}, {18.2f, 217.1f, -34.4f}, {24.1f, 230.2f, -27.5f}, {29.9f, 243.3f, -20.6f}, {35.8f, 256.4f, -13.7f}, {41.6f, 269.5f, -6.7f}, {46.6f, 282.5f, 1.1f}, {51.0f, 294.8f, 9.0f}, {54.6f, 307.4f, 18.1f}},
    {{-24.1f, 301.8f, -15.7f}, {-39.6f, 305.3f, -16.8f}, {-55.1f, 308.8f, -18.0f}, {-70.6f, 312.2f, -19.1f}, {-86.1f, 315.7f, -20.3f}, {-101.6f, 319.2f, -21.4f}, {-117.3f, 321.5f, -23.3f}, {-132.3f, 323.4f, -25.4f}, {-148.0f, 324.2f, -28.4f}},
    {{5.5f, 371.2f, -2.2f}, {7.9f, 380.7f, -14.8f}, {10.2f, 390.2f, -27.3f}, {12.6f, 399.7f, -39.9f}, {15.0f, 409.2f, -52.5f}, {17.3f, 418.7f, -65.1f}, {19.5f, 427.2f, -78.5f}, {21.4f, 434.8f, -91.5f}, {23.2f, 441.7f, -105.8f}},
    {{4.9f, 412.8f, 33.8f}, {9.0f, 427.6f, 29.6f}, {13.1f, 442.4f, 25.3f}, {17.2f, 457.2f, 21.1f}, {21.3f, 472.0f, 16.8f}, {25.4f, 486.8f, 12.6f}, {29.6f, 501.2f, 7.0f}, {33.6f, 514.7f, 1.3f}, {37.9f, 528.3f, -5.9f}},
    {{-7.5f, 410.8f, 31.7f}, {-7.5f, 426.4f, 28.8f}, {-7.6f, 442.0f, 25.8f}, {-7.6f, 457.7f, 22.8f}, {-7.6f, 473.3f, 19.8f}, {-7.7f, 489.0f, 16.8f}, {-7.5f, 504.4f, 12.5f}, {-7.2f, 518.9f, 8.0f}, {-6.7f, 533.7f, 1.9f}},
    {{-2.4f, -22.2f, -100.9f}, {-2.3f, -38.0f, -99.3f}, {-2.1f, -53.9f, -97.8f}, {-2.0f, -69.8f, -96.3f}, {-1.8f, -85.6f, -94.8f}, {-1.7f, -101.5f, -93.3f}, {-1.4f, -117.2f, -90.5f}, {-1.1f, -132.1f, -87.3f}, {-0.6f, -147.4f, -82.6f}},
    {{-2.6f, -22.2f, -100.9f}, {-2.5f, -38.0f, -99.4f}, {-2.4f, -53.9f, -97.9f}, {-2.3f, -69.8f, -96.4f}, {-2.2f, -85.6f, -94.9f}, {-2.1f, -101.5f, -93.4f}, {-1.8f, -117.2f, -90.6f}, {-1.5f, -132.1f, -87.5f}, {-1.1f, -147.4f, -82.8f}},
    {{-1.7f, -22.2f, -100.5f}, {-1.4f, -38.0f, -99.0f}, {-1.1f, -53.9f, -97.4f}, {-0.8f, -69.7f, -95.8f}, {-0.5f, -85.6f, -94.2f}, {-0.2f, -101.4f, -92.6f}, {0.3f, -117.1f, -89.7f}, {0.7f, -132.0f, -86.5f}, {1.4f, -147.3f, -81.8f}},
    {{-1.8f, -22.2f, -100.6f}, {-1.5f, -38.0f, -99.0f}, {-1.3f, -53.9f, -97.5f}, {-1.0f, -69.7f, -95.9f}, {-0.7f, -85.6f, -94.3f}, {-0.5f, -101.4f, -92.8f}, {-0.1f, -117.2f, -89.9f}, {0.4f, -132.0f, -86.7f}, {1.0f, -147.3f, -82.0f}},
};

// **Ours, the look tuned against MU's footage** (the user, 2026-10-25: "finetune requiem"; the
// Summoner's looks subtle and clear). MU draws these added on an 8-bit screen, where a bright
// sheet clips toward white; added linearly here the same sheets read darker and more saturated.
// Neil's body: a dark ghost in dark red armour, her sheet drawn dimmer and a little greyer.
constexpr float kNeilGhost[3] = {0.5f, 0.45f, 0.5f};   // added while she fades in, x her alpha
constexpr float kNeilBody[3] = {0.45f, 0.4f, 0.45f};   // alpha-blended at 0.7 once she stands
// Her nine blade flares: MU's red BITMAP_LIGHT at scale 1 washed her out; dimmer and smaller.
constexpr float kFlareLight = 0.35f;
constexpr float kFlareScale = 0.6f;
// The burning ground through Blend::Glow: pink over a white core.
constexpr float kGroundTint[3] = {1.0f, 0.55f, 0.65f};
// The thorns red, as the footage's: their metal tinted red, their red edges (gra, a grey
// gradient that reads half as bright linear as on MU's screen) laid twice.
constexpr float kThornMetal[3] = {0.85f, 0.25f, 0.25f};
constexpr int kThornEdgeLayers = 2;
// The bleed's mist: MU's fi01 is a dim sheet that its screen showed as small faint puffs.
constexpr float kMistLight = 0.55f;
constexpr float kMistSize = 0.75f;

}  // namespace

void Requiem::placeAt(const Placed& basis, const float model[3], float scale, float out[3]) const {
    for (int a = 0; a < 3; ++a) {
        out[a] = basis.at[a] +
                 (model[0] * basis.x[a] + model[1] * basis.y[a] + model[2] * basis.z[a]) * kUnit *
                     scale;
    }
}

float Requiem::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool Requiem::open(const std::string& assetDir, content::Textures& textures,
                   const content::Showing& table, const content::Ground* ground) {
    ground_ = ground;
    const std::string dir = assetDir + "/effects/requiem/";
    const auto take = [&](const std::string& path) -> bgfx::TextureHandle {
        if (!core::fileExists(path)) return BGFX_INVALID_HANDLE;
        return textures.load(path, content::TextureRole::Albedo);
    };
    const char* groups[kGroups] = {"maskreta01", "gra", "maskreta03"};
    bool whole = true;
    for (int k = 0; k < kKeys; ++k) {
        char name[32];
        std::snprintf(name, sizeof(name), "Neil01_%02d.obj", k);
        for (int g = 0; g < kGroups; ++g) {
            neil_[k][g].clear();
            loadEffectObj(dir + name, kUnit, groups[g], neil_[k][g]);
            whole = whole && !neil_[k][g].empty() && neil_[k][g].size() == neil_[0][g].size();
        }
    }
    for (int g = 0; g < kGroups; ++g) {
        neilSheet_[g] = take(dir + groups[g] + ".png");
        neilBlend_[g].resize(neil_[0][g].size());
    }
    whole = whole && bgfx::isValid(neilSheet_[0]);
    // The keys each thorn model changes at (Nife0N.json's poses).
    const int keys[kNifes][kNifePoses] = {{0, 1, 0}, {1, 2, 0}, {2, 3, 4}};
    const int counts[kNifes] = {2, 2, 3};
    size_t most[2] = {0, 0};
    for (int n = 0; n < kNifes; ++n) {
        nifePoses_[n] = counts[n];
        for (int p = 0; p < counts[n]; ++p) {
            nifeKeys_[n][p] = keys[n][p];
            const std::string pose = dir + "Nife0" + std::to_string(n + 1) + "_0" +
                                     std::to_string(keys[n][p]) + ".obj";
            for (int g = 0; g < 2; ++g) {
                nife_[n][p][g].clear();
                loadEffectObj(pose, kUnit, groups[g], nife_[n][p][g]);
                most[g] = std::max(most[g], nife_[n][p][g].size());
            }
        }
    }
    for (int g = 0; g < 2; ++g) nifeBlend_[g].resize(most[g]);
    const char* floors[3] = {"fury", "test2", "test2"};
    for (int n = 0; n < 3; ++n) {
        const std::string mesh = dir + "Ground0" + std::to_string(n + 1) + ".obj";
        groundMesh_[n][0].clear();
        groundMesh_[n][1].clear();
        loadEffectObj(mesh, kUnit, floors[n], groundMesh_[n][0]);
        loadEffectObj(mesh, kUnit, "bossrocklight_R", groundMesh_[n][1]);
        groundSheet_[n][0] = take(dir + floors[n] + ".png");
        groundSheet_[n][1] = take(dir + "bossrocklight_R.png");
    }
    const auto sheet = [&](const char* name) -> bgfx::TextureHandle {
        if (const content::EffectSheet* one = table.effect(name)) {
            return textures.load(assetDir + "/" + one->path, content::TextureRole::Albedo);
        }
        return BGFX_INVALID_HANDLE;
    };
    flare_ = sheet("light");         // BITMAP_LIGHT, Effect/flare01
    ribbon_ = sheet("trail_skill");  // BITMAP_BLUR + 2, Effect/motion_blur_r
    mist_ = sheet("requiem_mist");   // BITMAP_ADV_SMOKE, Effect/fi01
    core::logf("requiem: neil %s, thorns %s, ground %s, ribbon %s, mist %s", whole ? "yes" : "NO",
               nife_[2][2][0].empty() ? "NO" : "yes", groundMesh_[0][1].empty() ? "NO" : "yes",
               bgfx::isValid(ribbon_) ? "yes" : "NO", bgfx::isValid(mist_) ? "yes" : "NO");
    return whole;
}

bool Requiem::cast(const float caster[3], const float target[3], uint32_t caster_id) {
    if (playing_) return false;  // ours: one at a time (see the header)
    // CastSummonSkill turns him to the point; Neil faces his way, MU's -Y along it.
    float f[3] = {target[0] - caster[0], 0.0f, target[2] - caster[2]};
    const float flat = std::sqrt(f[0] * f[0] + f[2] * f[2]);
    if (flat > 1e-3f) {
        f[0] /= flat;
        f[2] /= flat;
    } else {
        f[0] = 0.0f;
        f[2] = 1.0f;
    }
    // The model's -Z (MU's -Y) on `f`, its Y up, its X to keep its hand: the blade in her right.
    Placed facing;
    for (int k = 0; k < 3; ++k) facing.z[k] = -f[k];
    facing.x[0] = f[2];
    facing.x[1] = 0.0f;
    facing.x[2] = -f[0];
    neilAt_ = facing;
    neilAt_.at[0] = caster[0] + f[0] * 100.0f * kUnit;
    neilAt_.at[2] = caster[2] + f[2] * 100.0f * kUnit;
    neilAt_.at[1] = (ground_ ? ground_->heightAt(neilAt_.at[0], neilAt_.at[2]) : caster[1]) +
                    10.0f * kUnit;
    targetAt_ = facing;
    for (int k = 0; k < 3; ++k) {
        targetAt_.at[k] = target[k];
        target_[k] = target[k];
    }
    playing_ = true;
    owed_ = 0.0f;
    frame_ = 0;
    clock_ = 0.0f;
    caster_ = caster_id;
    key_ = 0.0f;
    alpha_ = 0.0f;
    nifesUp_ = groundUp_ = thornsOwed_ = false;
    nifeAge_ = groundAge_ = 0;
    blurs_ = 0;
    blurLive_ = false;
    step();  // frame 0: what MU draws the frame it is made
    return true;
}

bool Requiem::takeThorns(float at[3], uint32_t& caster_id) {
    if (!thornsOwed_) return false;
    thornsOwed_ = false;
    for (int k = 0; k < 3; ++k) at[k] = target_[k];
    caster_id = caster_;
    return true;
}

void Requiem::mist(const float at[3]) {
    for (Mist& one : mists_) {
        if (one.alive) continue;
        one = Mist{};
        one.alive = true;
        for (int k = 0; k < 3; ++k) one.at[k] = at[k];
        // Angle[2] = rand() % 360, (0, 6-6.1, 0) turned by it, and 4 up.
        const float turn = unit() * 6.2831853f;
        const float out = 6.0f + unit() * 0.1f;
        one.push[0] = std::cos(turn) * out;
        one.push[2] = std::sin(turn) * out;
        one.push[1] = 4.0f;
        one.scale = float(int(unit() * 4.0f) + 8) * 0.01f;
        one.spin = unit() * 6.2831853f;
        return;
    }
}

void Requiem::blurAt(float key, float out[2][3]) const {
    const float at = std::clamp(key, 0.0f, 12.0f) * 10.0f;
    const int a = std::min(int(at), 119);
    const float t = at - float(a);
    for (int e = 0; e < 2; ++e) {
        float model[3];
        for (int k = 0; k < 3; ++k) {
            model[k] = kBladeEnds[a][e][k] + (kBladeEnds[a + 1][e][k] - kBladeEnds[a][e][k]) * t;
        }
        placeAt(neilAt_, model, 1.0f, out[e]);
    }
}

void Requiem::step() {
    // MoveObjectBlurs: one sample given up a frame, the whole gone 30 frames after the first.
    if (blurLive_) {
        --blurLife_;
        blurs_ = std::max(blurs_ - 1, 0);
        if (blurLife_ <= 0) {
            blurs_ = 0;
            blurLive_ = false;
        } else {
            for (int i = blurs_ - 1; i >= 0; --i) {
                std::memcpy(blur_[i + 1], blur_[i], sizeof(blur_[i]));
            }
        }
    }
    if (frame_ > 0) {
        // Move_MODEL_SUMMONER_SUMMON_NEIL: her fade, then her keys (PlayAnimation).
        const int life = 80 - frame_;
        if (life < 20) {
            alpha_ -= 0.05f;
        } else if (alpha_ < 0.7f) {
            alpha_ += 0.04f;
        }
        key_ = std::min(12.0f, key_ + 0.35f);
    }
    // The thorns past key 8 and the burning ground past key 10, each fading as its Move does.
    if (nifesUp_) {
        ++nifeAge_;
        if (50 - nifeAge_ < 20) nifeAlpha_ -= 0.05f;
    } else if (key_ > 8.0f) {
        nifesUp_ = true;
        nifeAge_ = 0;
        nifeAlpha_ = 1.0f;
    }
    if (groundUp_) {
        ++groundAge_;
        if (50 - groundAge_ < 20) {
            groundAlpha_ -= 0.05f;
        } else if (groundAlpha_ < 1.0f) {
            groundAlpha_ += 0.3f;
        }
    } else if (key_ > 10.0f) {
        groundUp_ = true;
        thornsOwed_ = true;
        groundAge_ = 0;
        groundAlpha_ = 0.0f;
        // GROUND1 60 units before Neil, and GROUND1-3 at the target.
        groundAt_[0] = neilAt_;
        const float before[3] = {0.0f, 0.0f, -60.0f};  // MU's (0, -60, 0): her -Z
        float at[3];
        placeAt(neilAt_, before, 1.0f, at);
        for (int k = 0; k < 3; ++k) groundAt_[0].at[k] = at[k];
        for (int n = 1; n < kGrounds; ++n) groundAt_[n] = targetAt_;
    }
    // The crescent's samples this frame: keys 0-10, ten between the last frame's key and this.
    if (key_ <= 10.0f && frame_ < 80) {
        const float speed = 0.35f;
        float k = key_ - speed;
        for (int i = 0; i < 10; ++i, k += speed / 10.0f) {
            if (!blurLive_) {
                blurLive_ = true;
                blurLife_ = 30;
                blurs_ = 0;
            }
            for (int j = blurs_ - 1; j >= 0; --j) std::memcpy(blur_[j + 1], blur_[j], sizeof(blur_[j]));
            blurAt(std::max(0.0f, k), blur_[0]);
            blurs_ = std::min(blurs_ + 1, kBlurMost - 1);
        }
    }
    ++frame_;
    const bool nifesGone = !nifesUp_ || nifeAge_ >= 50;
    const bool groundGone = !groundUp_ || groundAge_ >= 50;
    if (frame_ > 80 && nifesGone && groundGone && !blurLive_) playing_ = false;
}

void Requiem::update(float seconds) {
    if (playing_) {
        owed_ += seconds * kFps;
        clock_ += seconds;
        while (owed_ >= 1.0f && playing_) {
            owed_ -= 1.0f;
            step();
        }
    }
    const float frames = seconds * kFps;
    for (Mist& one : mists_) {
        if (!one.alive) continue;
        one.owed += frames;
        while (one.owed >= 1.0f && one.alive) {
            one.owed -= 1.0f;
            // MoveParticle's subtype 5.
            one.push[1] -= 0.4f;
            for (int k = 0; k < 3; ++k) one.at[k] += one.push[k] * kUnit;
            one.spin += 0.02f * one.life;
            one.scale += 0.04f;
            one.life -= 1.0f;
            if (one.life < 10.0f) one.alpha -= 0.1f;
            if (one.life < 20.0f) {
                one.push[0] *= 0.6f;
                one.push[2] *= 0.6f;
            }
            if (one.alpha < 0.0f || one.life <= 0.0f) one.alive = false;
        }
    }
}

void Requiem::blendPose(const std::vector<EffectCorner>& a, const std::vector<EffectCorner>& b,
                        float t, std::vector<EffectCorner>& out) const {
    const size_t n = std::min({a.size(), b.size(), out.size()});
    for (size_t i = 0; i < n; ++i) {
        out[i] = a[i];
        out[i].x = a[i].x + (b[i].x - a[i].x) * t;
        out[i].y = a[i].y + (b[i].y - a[i].y) * t;
        out[i].z = a[i].z + (b[i].z - a[i].z) * t;
    }
}

void Requiem::gather(gfx::Effects& effects) const {
    const float white[3] = {1.0f, 1.0f, 1.0f};
    if (playing_) {
        // Neil, while she lives (80 frames).
        const float alpha = std::clamp(alpha_, 0.0f, 1.0f);
        if (frame_ <= 80 && alpha > 0.0f) {
            const int k0 = std::min(int(key_), kKeys - 1), k1 = std::min(k0 + 1, kKeys - 1);
            const float t = key_ - float(k0);
            for (int g = 0; g < kGroups; ++g) {
                blendPose(neil_[k0][g], neil_[k1][g], t, neilBlend_[g]);
            }
            const bool ghost = alpha < 0.7f;
            for (const int g : {0, 2}) {
                if (ghost) {
                    const float lit[3] = {kNeilGhost[0] * alpha, kNeilGhost[1] * alpha,
                                          kNeilGhost[2] * alpha};
                    submitEffectAlong(effects, neilBlend_[g], neilSheet_[g], gfx::Blend::Additive,
                                      neilAt_.at, neilAt_.x, neilAt_.y, neilAt_.z, 1.0f, lit, 1.0f);
                } else {
                    submitEffectAlong(effects, neilBlend_[g], neilSheet_[g], gfx::Blend::Alpha,
                                      neilAt_.at, neilAt_.x, neilAt_.y, neilAt_.z, 1.0f, kNeilBody,
                                      alpha);
                }
            }
            const float red[3] = {alpha, 0.0f, 0.0f};
            submitEffectAlong(effects, neilBlend_[1], neilSheet_[1], gfx::Blend::Additive,
                              neilAt_.at, neilAt_.x, neilAt_.y, neilAt_.z, 1.0f, red, 1.0f);
            // Nine red flares on bones 51-59.
            if (bgfx::isValid(flare_)) {
                for (int b = 0; b < 9; ++b) {
                    float model[3];
                    for (int k = 0; k < 3; ++k) {
                        model[k] = kBladeBones[k0][b][k] +
                                   (kBladeBones[k1][b][k] - kBladeBones[k0][b][k]) * t;
                    }
                    gfx::Sprite sprite;
                    placeAt(neilAt_, model, 1.0f, sprite.position);
                    sprite.halfWidth = sprite.halfHeight = 0.5f * 64.0f * kUnit * kFlareScale;
                    sprite.colour[0] = kFlareLight;
                    sprite.colour[1] = 0.0f;
                    sprite.colour[2] = 0.0f;
                    sprite.sheet = flare_;
                    sprite.blend = gfx::Blend::Additive;
                    effects.add(sprite);
                }
            }
        }
        // The crescent.
        if (blurLive_ && blurs_ >= 2 && bgfx::isValid(ribbon_)) {
            const float n = float(blurs_);
            for (int j = 0; j + 1 < blurs_; ++j) {
                const float l0 = (n - float(j)) / n, l1 = (n - float(j + 1)) / n;
                const float u0 = float(j) / n, u1 = float(j + 1) / n;
                gfx::Sprite sprite;
                sprite.placed = true;
                sprite.sheet = ribbon_;
                sprite.blend = gfx::Blend::Additive;
                // One light for the quad, its middle's: the pass colours a sprite, not a corner.
                sprite.colour[0] = 0.5f * (l0 + l1);
                sprite.colour[1] = sprite.colour[2] = 0.0f;
                for (int k = 0; k < 3; ++k) {
                    sprite.corner[0][k] = blur_[j][0][k];
                    sprite.corner[1][k] = blur_[j][1][k];
                    sprite.corner[2][k] = blur_[j + 1][1][k];
                    sprite.corner[3][k] = blur_[j + 1][0][k];
                    sprite.position[k] = 0.25f * (blur_[j][0][k] + blur_[j][1][k] +
                                                  blur_[j + 1][0][k] + blur_[j + 1][1][k]);
                }
                const float uv[4][2] = {{u0, 1.0f}, {u0, 0.0f}, {u1, 0.0f}, {u1, 1.0f}};
                for (int k = 0; k < 4; ++k) {
                    sprite.cornerUv[k][0] = uv[k][0];
                    sprite.cornerUv[k][1] = uv[k][1];
                }
                effects.add(sprite);
            }
        }
        // The thorns, at the book's top tier all three.
        if (nifesUp_ && nifeAge_ < 50) {
            const float alpha = std::clamp(nifeAlpha_, 0.0f, 1.0f);
            const float key = std::min(5.0f, float(nifeAge_) * 0.3f);
            for (int n = 0; n < kNifes && alpha > 0.0f; ++n) {
                const int count = nifePoses_[n];
                int p0 = 0;
                while (p0 + 1 < count && float(nifeKeys_[n][p0 + 1]) <= key) ++p0;
                const int p1 = std::min(p0 + 1, count - 1);
                const float span = float(nifeKeys_[n][p1] - nifeKeys_[n][p0]);
                const float t = span > 0.0f
                                    ? std::clamp((key - float(nifeKeys_[n][p0])) / span, 0.0f, 1.0f)
                                    : 0.0f;
                for (int g = 0; g < 2; ++g) {
                    nifeBlend_[g].resize(nife_[n][p0][g].size());
                    blendPose(nife_[n][p0][g], nife_[n][p1][g], t, nifeBlend_[g]);
                }
                submitEffectAlong(effects, nifeBlend_[0], neilSheet_[0], gfx::Blend::Alpha,
                                  targetAt_.at, targetAt_.x, targetAt_.y, targetAt_.z, 1.0f,
                                  kThornMetal, alpha);
                const float red[3] = {alpha, 0.0f, 0.0f};
                for (int layer = 0; layer < kThornEdgeLayers; ++layer) {
                    submitEffectAlong(effects, nifeBlend_[1], neilSheet_[1], gfx::Blend::Additive,
                                      targetAt_.at, targetAt_.x, targetAt_.y, targetAt_.z, 1.0f,
                                      red, 1.0f);
                }
            }
        }
        // The burning ground.
        if (groundUp_ && groundAge_ < 50) {
            const float alpha = std::clamp(groundAlpha_, 0.0f, 1.0f);
            const float lit[3] = {kGroundTint[0] * alpha, kGroundTint[1] * alpha,
                                  kGroundTint[2] * alpha};
            const float scroll = -clock_ * 1000.0f * 0.001f;
            for (int i = 0; i < kGrounds && alpha > 0.0f; ++i) {
                const int n = i == 0 ? 0 : i - 1;
                const Placed& at = groundAt_[i];
                submitEffectAlong(effects, groundMesh_[n][0], groundSheet_[n][0],
                                  gfx::Blend::Glow, at.at, at.x, at.y, at.z, 1.0f, lit, 1.0f);
                submitEffectAlong(effects, groundMesh_[n][1], groundSheet_[n][1],
                                  gfx::Blend::Glow, at.at, at.x, at.y, at.z, 1.0f, lit, 1.0f,
                                  scroll);
            }
        }
    }
    // The bleed's mist.
    if (!bgfx::isValid(mist_)) return;
    for (const Mist& one : mists_) {
        if (!one.alive) continue;
        const float alpha = std::clamp(one.alpha, 0.0f, 1.0f);
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
        // MU's sprite is its sheet's 128 texels times its scale, in units.
        sprite.halfWidth = sprite.halfHeight = 0.5f * 128.0f * one.scale * kUnit * kMistSize;
        sprite.spin = one.spin;
        sprite.colour[0] = kMistLight * alpha;
        sprite.colour[1] = 0.0f;
        sprite.colour[2] = kMistLight * 0.6f * alpha;
        sprite.sheet = mist_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

}  // namespace mu::game
