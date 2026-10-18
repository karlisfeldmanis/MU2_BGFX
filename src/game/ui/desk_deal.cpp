// The desk's word on trades between players (sim/realm_deal.cpp), moved out of desk.cpp on
// 2026-10-11 to make room there for the Broker's window.
#include "game/ui/desk.h"

#include <string>

#include "game/ui/panel.h"

namespace mu::game {

// The realm's word on his trades, as lines in the chat and an ask on its card: MU's trade
// results said in the system log (CNewUITrade::ProcessToReceiveTradeResult).
void Desk::dealNews(Play& play) {
    for (const sim::Happening& said : play.takeDealNews()) {
        std::string name;
        if (!play.playerName(said.whom, &name, nullptr) || name.empty()) name = "They";
        const auto why = [](int32_t b) -> std::string {
            switch (sim::DealWhy(b)) {
                case sim::DealWhy::Busy: return "busy";
                case sim::DealWhy::Far: return "too far away";
                case sim::DealWhy::Dead: return "dead";
                case sim::DealWhy::Here: return "not allowed here";
                case sim::DealWhy::Gone: return "gone";
                case sim::DealWhy::Closed: return "closed";
                case sim::DealWhy::Zen: return "short of the Zen offered";
                default: return "not possible";
            }
        };
        switch (sim::DealEvent(said.a)) {
            case sim::DealEvent::Asked:
                // Said to both: the one asked holds the ask.
                if (play.realm().dealAskedBy() == said.whom) {
                    if (chat_.askedToTrade(name, said.whom)) play.ui(Play::Ui::Invited);
                } else {
                    chat_.notice("You asked " + name + " to trade.");
                }
                break;
            case sim::DealEvent::Refused:
                chat_.notice(sim::DealWhy(said.b) == sim::DealWhy::Here ? std::string("You cannot trade here.")
                             : sim::DealWhy(said.b) == sim::DealWhy::Nobody ? std::string("There is no one to trade with.")
                             : "Trade refused: " + why(said.b) + ".");
                play.ui(Play::Ui::Refused);
                break;
            case sim::DealEvent::Declined:
                chat_.notice(said.b == 1 ? name + " did not answer." : name + " declined to trade.");
                break;
            case sim::DealEvent::Opened: chat_.notice("Trading with " + name + "."); break;
            case sim::DealEvent::Cancelled: chat_.notice("The trade with " + name + " was cancelled (" + why(said.b) + ")."); break;
            case sim::DealEvent::NoRoom:
                chat_.notice(said.b == 1 ? std::string("No room in your bag for the trade.")
                                         : name + " has no room for the trade.");
                play.ui(Play::Ui::Refused);
                break;
            case sim::DealEvent::Done: {
                std::string got;
                if (said.b > 0) got = std::to_string(said.b) + (said.b == 1 ? " item" : " items");
                if (said.c > 0) got += (got.empty() ? "" : " and ") + panel::commas(said.c) + " Zen";
                chat_.notice("Traded with " + name + (got.empty() ? std::string(".") : ": you got " + got + "."));
                play.ui(Play::Ui::Took);
                break;
            }
        }
    }
}

}  // namespace mu::game
