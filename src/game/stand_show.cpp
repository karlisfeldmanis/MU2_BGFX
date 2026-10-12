#include "game/stand_show.h"

#include <cmath>

#include "content/showing.h"
#include "core/log.h"
#include "game/crowd.h"
#include "game/figures.h"
#include "game/play_tuning.h"

namespace mu::game {
namespace {

int boneNamed(const std::vector<content::Bone>& bones, const std::string& name) {
    for (size_t b = 0; b < bones.size(); ++b) {
        if (bones[b].name == name) return int(b);
    }
    return -1;
}

}  // namespace

bool StandShow::open(const std::string& assetDir, content::Textures& textures,
                     const content::Showing& table) {
    // As PlayMode opens Play's own (play_mode.cpp), from the showing's table.
    open_ = stars_.open(assetDir, textures, table) && trails_.open(assetDir, textures, table) &&
            flames_.open(assetDir, textures, table);
    return open_;
}

void StandShow::shutdown() {
    stars_.shutdown();
    trails_.shutdown();
    flames_.shutdown();
    body_ = nullptr;
    open_ = false;
}

uint32_t StandShow::dice() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return dice_;
}

void StandShow::fit(const Figure& figure) {
    // Play::fit's reading (play_open.cpp), the standing half of it.
    body_ = figure.body();
    aura_ = -1;
    starBones_.clear();
    starTint_ = nullptr;
    arcBones_.clear();
    shadeBones_.clear();
    embers_ = false;
    emberBone_ = -1;
    trail_[0] = trail_[1] = -1;
    for (int& h : handFlame_) h = -1;
    burns_ = false;
    handOwed_ = burnOwed_ = emberOwed_ = arcStep_ = 0.0f;
    if (!body_ || !body_->skeletonMesh) return;
    const FigureBody& look = *body_;
    const std::vector<content::Bone>& bones = look.skeletonMesh->bones();

    if (!look.auraBone.empty()) {
        aura_ = boneNamed(bones, look.auraBone);
        for (int i = 0; i < 3; ++i) auraColour_[i] = look.auraColour[i];
    }
    // The stars: the Alquamos's ribbons' bones, the Death Tree's horns, the Queen's and the
    // Drakan's own, each with its tint where the recipe gives one.
    const bool queen = look.has("queen_rainer"), drakan = look.has("drakan");
    if (look.has("star_ribbons") || look.has("death_tree") || queen || drakan) {
        for (const std::string& name : look.bonesFor("stars")) {
            if (const int b = boneNamed(bones, name); b >= 0) starBones_.push_back(b);
        }
        if (!look.has("star_ribbons")) starTint_ = look.starTint();
    }
    if (queen || drakan) {
        for (const auto& pair : look.arcPairs) {
            const int a = boneNamed(bones, pair[0]), b = boneNamed(bones, pair[1]);
            if (a < 0 || b < 0) continue;
            arcBones_.push_back(a);
            arcBones_.push_back(b);
        }
        arcsAFrame_ = look.arcsAFrame;
        arcHalf_ = look.arcHalf;
    }
    // The Shadows' joints; the Gorgon's, the Death Knight's and the Great Drakan's embers.
    shadePoison_ = look.has("poison_shadow");
    embers_ = look.has("death_gorgon") || look.has("death_knight") || look.has("great_drakan");
    emberEvery_ = look.has("death_knight")   ? kKnightEmberEveryFrames
                  : look.has("great_drakan") ? kGreatDrakanEmberEvery
                                             : kEmberEveryFrames;
    if (look.has("great_drakan") || look.has("death_knight")) {
        for (size_t b = 0; b < bones.size(); ++b) {
            if (look.named("ember", 0, bones[b].name)) emberBone_ = int(b);
        }
    }
    if (embers_) {
        for (size_t b = 0; b < bones.size(); ++b) {
            const std::string& name = bones[b].name;
            if (name == "Bip01" || name == "Bip01 Footsteps" || name == "boss" ||
                name.rfind("dummy", 0) == 0) {
                continue;
            }
            shadeBones_.push_back(int(b));
        }
    }
    if (look.has("shadow") || look.has("poison_shadow")) {
        for (size_t b = 0; b < bones.size(); ++b) {
            for (const std::string& name : look.bonesFor("shade")) {
                if (bones[b].name == name) shadeBones_.push_back(int(b));
            }
        }
    }
    // Tarkan's eye trails, the Beam Knight's hand flames, the Death Beam Knight's burning.
    if (!look.eyeTrail[0].empty()) {
        trail_[0] = boneNamed(bones, look.eyeTrail[0]);
        trail_[1] = boneNamed(bones, look.eyeTrail[1]);
    }
    if (look.has("hand_flame")) {
        for (size_t b = 0; b < bones.size(); ++b) {
            for (int h = 0; h < 4; ++h) {
                if (look.named("hand_flame", size_t(h), bones[b].name)) handFlame_[h] = int(b);
            }
        }
    }
    if (look.has("burning")) {
        int found = 0;
        for (int i = 0; i < 35; ++i) burnBones_[i] = -1;
        for (size_t b = 0; b < bones.size(); ++b) {
            for (int i = 0; i < 35; ++i) {
                if (look.named("burning", size_t(i), bones[b].name)) {
                    burnBones_[i] = int(b);
                    ++found;
                }
            }
        }
        burns_ = found == 35;
    }
    core::logf("stand show: %s -- aura %d, %zu stars, %zu arcs, %zu shade joints%s, eye trails %d %d, "
               "hand flames %d, burning %d",
               look.name.c_str(), aura_, starBones_.size(), arcBones_.size() / 2, shadeBones_.size(),
               embers_ ? " (embers)" : "", trail_[0], trail_[1], handFlame_[3] >= 0, burns_);
}

void StandShow::update(float seconds, const Figure& figure) {
    // Play::snort's and Play::shade's feeds (play_show.cpp), for one body alive and whole.
    stars_.update(seconds);
    trails_.update(seconds);
    flames_.update(seconds);
    clock_ += seconds;
    if (!body_ || figure.body() != body_) return;
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    const float frames = seconds * 25.0f;
    float at[3];

    if (aura_ >= 0 && figure.pointOn(aura_, origin, at)) stars_.glow(at, 1.0f, auraColour_);
    if (!starBones_.empty()) {
        const float luminosity = 0.85f + 0.15f * std::sin(clock_ * 3.0f);
        for (const int bone : starBones_) {
            if (figure.pointOn(bone, origin, at)) stars_.starlight(at, 1.0f, luminosity, starTint_);
        }
    }
    if (!arcBones_.empty()) {
        const int pairs = int(arcBones_.size() / 2);
        arcStep_ -= frames;
        if (arcStep_ <= 0.0f) {
            arcStep_ += 1.0f;
            if (arcStep_ <= 0.0f) arcStep_ = 1.0f;
            arcLit_ = 0;
            for (int k = 0; k < arcsAFrame_; ++k) arcLit_ |= 1u << (dice() % uint32_t(pairs));
        }
        float colour[3];
        for (int k = 0; k < 3; ++k) colour[k] = kQueenArcColour[k] * kQueenArcDim;
        for (int p = 0; p < pairs; ++p) {
            if (!(arcLit_ & (1u << p))) continue;
            float a[3], b[3];
            if (figure.pointOn(arcBones_[size_t(2 * p)], origin, a) &&
                figure.pointOn(arcBones_[size_t(2 * p + 1)], origin, b)) {
                stars_.thunderBeam(a, b, arcHalf_, colour);
            }
        }
    }
    if (embers_) {
        emberOwed_ += frames / emberEvery_;
        while (emberOwed_ >= 1.0f) {
            emberOwed_ -= 1.0f;
            const int bone = emberBone_ >= 0 ? emberBone_
                             : shadeBones_.empty() ? -1
                                                   : shadeBones_[dice() % uint32_t(shadeBones_.size())];
            if (bone < 0) break;
            if (figure.pointOn(bone, origin, at)) stars_.ember(at);
        }
    } else {
        for (const int bone : shadeBones_) {
            if (figure.pointOn(bone, origin, at)) stars_.star(at, shadePoison_, 1.0f);
        }
    }
    if (trail_[0] >= 0 && trail_[1] >= 0) {
        if (figure.pointOn(trail_[0], origin, at)) trails_.feed(1, false, at);
        if (figure.pointOn(trail_[1], origin, at)) trails_.feed(1, true, at);
    }
    if (handFlame_[3] >= 0) {
        handOwed_ += frames;
        while (handOwed_ >= 1.0f) {
            handOwed_ -= 1.0f;
            for (int h = 0; h < 4; h += 2) {
                float root[3], claw[3];
                if (!figure.pointOn(handFlame_[h], origin, root) ||
                    !figure.pointOn(handFlame_[h + 1], origin, claw)) {
                    continue;
                }
                float along[3] = {claw[0] - root[0], claw[1] - root[1], claw[2] - root[2]};
                const float n = std::sqrt(along[0] * along[0] + along[1] * along[1] + along[2] * along[2]);
                if (n < 1e-5f) continue;
                for (float& a : along) a /= n;
                flames_.handFlame(claw, along, kHandFlameScale);
            }
        }
    }
    if (burns_) {
        burnOwed_ += frames;
        while (burnOwed_ >= 1.0f) {
            burnOwed_ -= 1.0f;
            ++burnFrame_;
            float points[35][3];
            bool posed = true;
            for (int i = 0; i < 35 && posed; ++i) posed = figure.pointOn(burnBones_[i], origin, points[i]);
            if (!posed) continue;
            for (const FigureBody::BurnSegment& s : body_->burnSegments) {
                flames_.segmentFlame(points[s.from], points[s.to], s.scale, kBurnLight);
            }
            if (burnFrame_ % 2 == 0) {
                flames_.segmentFlame(points[body_->burnBody[0]], points[body_->burnBody[1]], 1.3f,
                                     kBurnLight);
                flames_.stillFlame(points[body_->burnHead], 0.5f, kBurnLight);
            }
        }
    }
}

void StandShow::gather(gfx::Effects& effects, const float* eye) const {
    trails_.gather(effects, eye);
    flames_.gather(effects);
    stars_.gather(effects);
}

uint32_t StandShow::lights(gfx::PointLight* out, uint32_t max, const float near[3]) const {
    return stars_.lights(out, max, near);
}

}  // namespace mu::game
