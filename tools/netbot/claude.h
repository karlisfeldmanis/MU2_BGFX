#pragma once

// **The Claude bot** (netbot --claude DIR): one character played by Claude, the model, through
// three files in DIR. His brain (tools/bot/brain.h) still swings, drinks, shops and walks, as
// every net bot's does, since a model's turn is seconds long and a fight is not. What the model
// decides is the slow part: where he hunts, what he says, who he parties with.
//
//   state.txt   rewritten each second: how he stands, what is near, his quests, his party
//   heard.log   every chat line he hears or says, appended, with the realm's clock
//   orders.txt  appended by the model, one order a line, read from where it was last read:
//
//     say TEXT              to his map
//     party TEXT            to his party
//     whisper NAME TEXT     to one player
//     invite NAME | accept NAME | decline NAME | leave | kick NAME
//     go WORLD              grind WORLD (lorencia, dungeon, devias, noria, losttower, atlans,
//                           tarkan, icarus) whatever the brain would choose
//     auto                  the brain's own choice again
//
// Nothing he hears is answered for him: talk.h's lines are off.

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace mu::bot {

class Claude {
public:
    explicit Claude(std::string dir) : dir_(std::move(dir)) {
        std::error_code ignored;
        std::filesystem::create_directories(dir_, ignored);
        // Orders written before he came are not his: read from the end as it stands.
        std::ifstream in(dir_ + "/orders.txt", std::ios::ate);
        if (in) read_ = in.tellg();
    }

    // The orders appended since the last call, a line each, blank lines skipped.
    std::vector<std::string> orders() {
        std::vector<std::string> out;
        std::ifstream in(dir_ + "/orders.txt");
        if (!in) return out;
        in.seekg(0, std::ios::end);
        const std::streamoff size = in.tellg();
        if (size < read_) read_ = 0;  // the file was started again
        in.seekg(read_);
        std::string line;
        while (std::getline(in, line)) {
            if (in.eof()) break;  // a line not yet ended is not yet whole
            read_ = in.tellg();
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (!line.empty()) out.push_back(line);
        }
        return out;
    }

    void note(const std::string& clock, const std::string& text) {
        std::ofstream out(dir_ + "/heard.log", std::ios::app);
        out << "[" << clock << "] " << text << "\n";
    }

    // The state, whole: written beside and moved in, so it is never read half written.
    void state(const std::string& text) {
        const std::string path = dir_ + "/state.txt";
        {
            std::ofstream out(path + ".new");
            out << text;
        }
        std::error_code ignored;
        std::filesystem::rename(path + ".new", path, ignored);
    }

private:
    std::string dir_;
    std::streamoff read_ = 0;
};

}  // namespace mu::bot
