// The bot: a character played from level 1 with no window, the way a player would -- hunting
// what his level can take, drinking potions, resting in town when he is low, picking up what
// falls, wearing what is better, learning the orbs he can read, walking to town to sell, repair,
// restock and buy gear, and doing the quests: Marlon's in Lorencia, Peia's in Noria, Apostle
// Devin's in Devias and the Golden Archer's three floors of the Dungeon. It writes what happened,
// and when.
//
// The hand is the bot's and not the sim's: it only asks the realm what a player may ask --
// requests, potion right-clicks, drags, counters, a giver's dialog, a gate walked into or a trip
// paid for -- so whatever it finds (a jewel's wait, a breed that kills him, a quest he cannot
// finish) is the game's answer and not a shortcut's. A map change is the mode's in the game
// (PlayMode::travel): the hero's record is taken and laid on a realm raised on the next world,
// and the bot does exactly that. The quests' twelve hours run on a wall clock the bot keeps off
// its own ticks, so a long run sees them come back.
//
// Not the seeded hunt: `mu2 --headless` is the log the tests compare, and this changes nothing
// in it. The realm's own log is silenced for the hours of play; BOT_LOG=1 lets it through, which
// is where a skill pressed and never thrown says why.
//
//   build/bot [--kin dk|dw|elf|mg] [--path melee|magic] [--seed N] [--runs N] [--hours H] [--until-jewel]
//             [--no-quests] [--fights] [--build s,a,v,e] [--no-shop-skills] [--quiet]

#include "brain.h"

int main(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--kin") {
            const std::string k = next();
            options.kin = k == "dw"    ? sim::Kin::DarkWizard
                          : k == "elf" ? sim::Kin::FairyElf
                          : k == "mg"  ? sim::Kin::MagicGladiator
                                       : sim::Kin::DarkKnight;
        } else if (a == "--path") options.magic = std::string(next()) == "magic"; else if (a == "--seed") options.seed = std::strtoull(next(), nullptr, 10);
        else if (a == "--runs") options.runs = std::max(1, std::atoi(next()));
        else if (a == "--hours") options.hours = std::atof(next());
        else if (a == "--until-jewel") options.untilJewel = true;
        else if (a == "--no-quests") options.quests = false;
        else if (a == "--fights") options.fights = true;
        else if (a == "--no-shop-skills") options.noShopSkills = true;
        else if (a == "--pet") {
            const std::string k = next();
            options.pet = k == "angel" ? 0 : k == "imp" ? 1 : -2;
        }
        else if (a == "--build") {
            std::sscanf(next(), "%d,%d,%d,%d", &options.build[0], &options.build[1], &options.build[2],
                        &options.build[3]);
        }
        else if (a == "--quiet") options.quiet = true;
        else {
            std::printf("usage: bot [--kin dk|dw|elf|mg] [--path melee|magic] [--seed N] [--runs N] [--hours H] "
                        "[--until-jewel] [--no-quests] [--fights] [--build s,a,v,e] [--no-shop-skills] [--quiet]\n");
            return 2;
        }
    }
    if (!std::getenv("BOT_LOG")) core::logSilence(true);

    std::vector<Outcome> outcomes;
    for (int run = 0; run < options.runs; ++run) {
        const uint64_t seed = options.seed + uint64_t(run);
        std::printf("%s%s, seed %llu\n", kinName(options.kin),
                    options.kin != sim::Kin::MagicGladiator ? "" : options.magic ? " (magic)" : " (melee)",
                    (unsigned long long)seed);
        Bot bot(options, seed);
        if (!bot.start()) {
            std::printf("bot: the realm did not raise\n");
            return 1;
        }
        const int64_t cap = int64_t(options.hours * 3600.0 * 20.0);
        while (bot.now() < cap && !(options.untilJewel && bot.foundJewel())) bot.tick();
        const Outcome o = bot.finish();
        std::printf("  after %s: level %d, %d kills, %d deaths, %lld zen, %d trips to town, "
                    "%d bought, %d sold, %d potions drunk, %d jewels, %d runes, %d map changes\n",
                    clock(o.ticks).c_str(), o.level, o.kills, o.deaths, (long long)o.zen, o.trips,
                    o.bought, o.sold, o.drunk, o.jewels, o.runes, o.maps);
        if (o.firstJewelTick >= 0) {
            std::printf("  first jewel: %s at %s, kill %d, level %d\n", o.firstJewel.c_str(),
                        clock(o.firstJewelTick).c_str(), o.firstJewelKills, o.firstJewelLevel);
        } else {
            std::printf("  no jewel\n");
        }
        for (int q = 0; q < sim::kQuests; ++q) {
            if (o.firstHandIn[q] >= 0) {
                std::printf("  first hand-in: %s at %s\n", sim::questAt(q).title,
                            clock(o.firstHandIn[q]).c_str());
            }
        }
        bot.printKills();
        outcomes.push_back(o);
    }

    if (outcomes.size() > 1) {
        std::vector<double> minutes, kills;
        int none = 0;
        for (const Outcome& o : outcomes) {
            if (o.firstJewelTick < 0) {
                ++none;
                continue;
            }
            minutes.push_back(double(o.firstJewelTick) / 1200.0);
            kills.push_back(o.firstJewelKills);
        }
        std::sort(minutes.begin(), minutes.end());
        std::sort(kills.begin(), kills.end());
        std::printf("\n%zu runs: first jewel in %zu", outcomes.size(), minutes.size());
        if (!minutes.empty()) {
            std::printf(" -- median %.0f min (%.0f-%.0f), median %.0f kills",
                        minutes[minutes.size() / 2], minutes.front(), minutes.back(),
                        kills[kills.size() / 2]);
        }
        std::printf("; none in %d\n", none);
        for (int q = 0; q < sim::kQuests; ++q) {
            std::vector<double> at;
            for (const Outcome& o : outcomes) {
                if (o.firstHandIn[q] >= 0) at.push_back(double(o.firstHandIn[q]) / 1200.0);
            }
            if (at.empty()) continue;
            std::sort(at.begin(), at.end());
            std::printf("%s: first handed in by %zu of %zu, median %.0f min (%.0f-%.0f)\n",
                        sim::questAt(q).title, at.size(), outcomes.size(), at[at.size() / 2],
                        at.front(), at.back());
        }
    }
    return 0;
}
