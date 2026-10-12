// A figure's shows, cooked/figures/shows.json (sprint E4): each monster recipe's `show` -- its
// tags, aura, eye trails, bones, star tint, arcs, heads, beam material, claws and burn -- read
// once and given to every body Figures::addMonster makes. Out of figures.cpp, whose ratchet
// (tools/sizecheck.py) they had grown past.
#include "game/figures.h"

#include <memory>
#include <string>
#include <vector>

#include "core/files.h"
#include "core/json.h"
#include "core/log.h"

namespace mu::game {

void Figures::readShows() {
    if (showsRead_) return;
    showsRead_ = true;
    const std::string path = core::join(assetDir_, "cooked/figures/shows.json");
    core::Json shows = core::parseJsonFile(path);
    if (shows.isNull()) {
        core::logf("no %s: every monster draws without its own shows. tools/cook.py --only shows",
                   path.c_str());
        return;
    }
    for (const auto& [name, row] : shows["shows"].members) {
        Show& show = shows_[name];
        for (const core::Json& tag : row["tags"].items) show.tags.push_back(tag.string);
        show.auraBone = row["aura"]["bone"].stringOr("");
        for (int i = 0; i < 3 && i < int(row["aura"]["colour"].items.size()); ++i) {
            show.auraColour[i] = float(row["aura"]["colour"].items[size_t(i)].numberOr(0.0));
        }
        show.eyeTrail[0] = row["eye_trail"]["right"].stringOr("");
        show.eyeTrail[1] = row["eye_trail"]["left"].stringOr("");
        for (const auto& [role, names] : row["bones"].members) {
            std::vector<std::string> list;
            for (const core::Json& name : names.items) list.push_back(name.string);
            show.boneRoles.emplace_back(role, std::move(list));
        }
        const core::Json& tint = row["star_tint"];
        show.tinted = tint.items.size() == 3;
        for (int i = 0; i < 3 && show.tinted; ++i) {
            show.tint[i] = float(tint.items[size_t(i)].numberOr(0.0));
        }
        const auto pairsOf = [](const core::Json& list) {
            std::vector<std::array<std::string, 2>> out;
            for (const core::Json& pair : list.items) {
                if (pair.items.size() != 2) continue;
                out.push_back({pair.items[0].string, pair.items[1].string});
            }
            return out;
        };
        show.arcPairs = pairsOf(row["arcs"]["pairs"]);
        show.arcsAFrame = int(row["arcs"]["a_frame"].numberOr(0.0));
        show.arcHalf = float(row["arcs"]["half"].numberOr(0.0));
        for (const core::Json& head : row["heads"].items) {
            FigureBody::Head one;
            one.bone = head["bone"].string;
            for (int i = 0; i < 3 && i < int(head["mouth"].items.size()); ++i) {
                one.mouth[i] = float(head["mouth"].items[size_t(i)].numberOr(0.0));
            }
            show.heads.push_back(one);
        }
        show.beamMaterial = row["beam_material"].stringOr("");
        const core::Json& claws = row["claws"];
        show.claws.pairs = pairsOf(claws["pairs"]);
        show.claws.from = float(claws["from"].numberOr(0.0));
        show.claws.to = float(claws["to"].numberOr(0.0));
        for (int i = 0; i < 3 && i < int(claws["light"].items.size()); ++i) {
            show.claws.light[i] = float(claws["light"].items[size_t(i)].numberOr(0.0));
        }
        show.claws.until = float(claws["until"].numberOr(-1.0));
        show.landsAt = float(row["lands"].numberOr(-1.0));
        for (const core::Json& segment : row["burn"]["segments"].items) {
            if (segment.items.size() != 3) continue;
            show.burnSegments.push_back({int(segment.items[0].numberOr(0.0)),
                                         int(segment.items[1].numberOr(0.0)),
                                         float(segment.items[2].numberOr(1.0))});
        }
        for (int i = 0; i < 2 && i < int(row["burn"]["body"].items.size()); ++i) {
            show.burnBody[i] = int(row["burn"]["body"].items[size_t(i)].numberOr(0.0));
        }
        show.burnHead = int(row["burn"]["head"].numberOr(0.0));
        const core::Json& inferno = row["inferno"];
        show.inferno.throws = !inferno.isNull();
        show.inferno.bombs = inferno["bombs"].boolOr(false);
        show.inferno.mesh = inferno["mesh"].boolOr(false);
        show.inferno.first = inferno["first"].boolOr(false);
        show.inferno.frame = int(inferno["attack_frame"].numberOr(0.0));
        show.blow = row["blow"].stringOr("");
    }
}

void Figures::addShown(std::unique_ptr<FigureBody> made) {
    FigureBody& body = *made;
    readShows();
    if (auto shown = shows_.find(body.name); shown != shows_.end()) {
        const Show& show = shown->second;
        body.tags = show.tags;
        body.auraBone = show.auraBone;
        for (int i = 0; i < 3; ++i) body.auraColour[i] = show.auraColour[i];
        body.eyeTrail[0] = show.eyeTrail[0];
        body.eyeTrail[1] = show.eyeTrail[1];
        body.boneRoles = show.boneRoles;
        for (int i = 0; i < 3; ++i) body.tint[i] = show.tint[i];
        body.tinted = show.tinted;
        body.arcPairs = show.arcPairs;
        body.arcsAFrame = show.arcsAFrame;
        body.arcHalf = show.arcHalf;
        body.heads = show.heads;
        body.beamMaterial = show.beamMaterial;
        body.claws = show.claws;
        body.landsAt = show.landsAt;
        body.burnSegments = show.burnSegments;
        body.burnBody[0] = show.burnBody[0];
        body.burnBody[1] = show.burnBody[1];
        body.burnHead = show.burnHead;
        body.inferno = show.inferno;
        body.blow = show.blow;
    }
    bodies_[body.name] = std::move(made);
}

}  // namespace mu::game
