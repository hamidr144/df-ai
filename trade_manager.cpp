#include "ai.h"
#include "population.h"
#include "stocks.h"
#include "trade.h"

#include "modules/Gui.h"
#include "modules/Materials.h"
#include "modules/Units.h"

//#include "df/assign_trade_status.h"
#include "df/building_tradedepotst.h"
#include "df/caravan_state.h"
#include "df/creature_raw.h"
#include "df/entity_position_assignment.h"
#include "df/entity_raw.h"
#include "df/ethic_response.h"
#include "df/ethic_type.h"
#include "df/historical_entity.h"
#include "df/historical_figure.h"
#include "df/item.h"
#include "df/job.h"
//#include "df/plotinfo.h"
//#include "df/viewscreen_layer_assigntradest.h"
//#include "df/viewscreen_tradegoodsst.h"
//#include "df/viewscreen_tradelistst.h"
#include "df/world.h"

REQUIRE_GLOBAL(plotinfo);
REQUIRE_GLOBAL(world);

void Population::update_trading(color_ostream & out)
{
    bool any_traders = ai.trade.can_move_goods();

    if (any_traders && did_trade)
    {
        return;
    }

    if (!any_traders)
    {
        did_trade = false;
    }

    if (set_up_trading(out, any_traders))
    {
        if (any_traders)
        {
            ai.debug(out, "[trade] Requested broker at depot.");
        }
        else
        {
            ai.debug(out, "[trade] Dismissed broker from depot: no traders");
        }
        return;
    }

    if (!any_traders)
    {
        return;
    }

    df::caravan_state *caravan = nullptr;
    for (auto it = plotinfo->caravans.begin(); it != plotinfo->caravans.end(); it++)
    {
        if ((*it)->trade_state == df::caravan_state::AtDepot)
        {
            caravan = *it;
        }
        else if ((*it)->trade_state == df::caravan_state::Approaching)
        {
            ai.debug(out, "[trade] Waiting for the traders from " + AI::describe_name(df::historical_entity::find((*it)->entity)->name) + " to arrive at the depot...");
        }
    }

    if (!caravan)
    {
        return;
    }

    auto broker_pos = std::find_if(plotinfo->main.fortress_entity->positions.own.begin(), plotinfo->main.fortress_entity->positions.own.end(), [](df::entity_position *pos) -> bool { return pos->responsibilities[entity_position_responsibility::TRADE]; });
    if (broker_pos == plotinfo->main.fortress_entity->positions.own.end())
    {
        ai.debug(out, "[trade] Could not find broker position!");
        return;
    }

    auto broker_assignment = std::find_if(plotinfo->main.fortress_entity->positions.assignments.begin(), plotinfo->main.fortress_entity->positions.assignments.end(), [broker_pos](df::entity_position_assignment *asn) -> bool { return asn->position_id == (*broker_pos)->id; });
    if (broker_assignment == plotinfo->main.fortress_entity->positions.assignments.end())
    {
        ai.debug(out, "[trade] Could not find broker assignment!");
        return;
    }

    df::unit *broker = nullptr;

    for (auto j = world->jobs.list.next; j; j = j->next)
    {
        if (j->item->job_type == job_type::TradeAtDepot)
        {
            for (auto ref : j->item->general_refs)
            {
                if (ref->getType() == general_ref_type::UNIT_WORKER)
                {
                    broker = ref->getUnit();
                    if (broker)
                    {
                        break;
                    }
                }
            }
            if (broker)
            {
                break;
            }
        }
    }

    if (!broker)
    {
        df::historical_figure *broker_hf = df::historical_figure::find((*broker_assignment)->histfig);
        if (!broker_hf)
        {
            ai.debug(out, "[trade] Could not find broker!");
            return;
        }

        broker = df::unit::find(broker_hf->unit_id);
        if (!broker)
        {
            ai.debug(out, "[trade] Could not find broker unit!");
            return;
        }

        if (!broker->job.current_job || broker->job.current_job->job_type != job_type::TradeAtDepot)
        {
            ai.debug(out, "[trade] Waiting for the broker to do " + std::string(ENUM_ATTR(pronoun_type, posessive, broker->sex)) + " job: " + AI::describe_unit(broker) + " (currently: " + AI::describe_job(broker) + ") " + AI::describe_room(ai.find_room_at(Units::getPosition(broker))));
            if (caravan->time_remaining < 1000 && set_up_trading(out, true, true))
            {
                ai.debug(out, "[trade] Broker took too long. Allowing any dwarf to trade this season.");
            }
            return;
        }
    }

    room *depot = ai.find_room(room_type::tradedepot, [broker](room *r) -> bool { return r->include(Units::getPosition(broker)); });
    if (!depot)
    {
        ai.debug(out, "[trade] Broker en route to depot: " + AI::describe_unit(broker) + " (Currently at " + AI::describe_room(ai.find_room_at(Units::getPosition(broker))) + ")");
        return;
    }

    int32_t waiting_for_items = 0;
    for (auto j = world->jobs.list.next; j; j = j->next)
    {
        if (j->item->job_type == job_type::BringItemToDepot)
        {
            waiting_for_items++;
        }
    }

    if (waiting_for_items)
    {
        if (caravan->time_remaining < 1000)
        {
            ai.debug(out, stl_sprintf("[trade] Waiting for %d more items to arrive at the depot, but time is running out. Trading with what we have.", waiting_for_items));
        }
        else
        {
            ai.debug(out, stl_sprintf("[trade] Waiting for %d more items to arrive at the depot.", waiting_for_items));
            return;
        }
    }

    if (perform_trade(out))
    {
        did_trade = true;

        if (set_up_trading(out, false))
        {
            ai.debug(out, "[trade] Dismissed broker from depot: finished trade");
        }
    }
}

bool Population::set_up_trading(color_ostream & out, bool should_be_trading, bool allow_any_dwarf)
{
    room *r = ai.find_room(room_type::tradedepot);
    if (!r)
    {
        return false;
    }
    df::building_tradedepotst *bld = virtual_cast<df::building_tradedepotst>(r->dfbuilding());
    if (!bld || bld->getBuildStage() < bld->getMaxBuildStage())
    {
        return false;
    }
    bool toggle_restrict = (should_be_trading && allow_any_dwarf && bld->trade_flags.bits.trader_requested && !bld->trade_flags.bits.anyone_can_trade) || (!should_be_trading && bld->trade_flags.bits.anyone_can_trade);
    if (bld->trade_flags.bits.trader_requested == should_be_trading && !toggle_restrict)
    {
        return false;
    }

    if (!AI::is_dwarfmode_viewscreen())
    {
        return false;
    }

    int32_t start_x, start_y, start_z;
    Gui::getViewCoords(start_x, start_y, start_z);

    //Gui::getCurViewscreen(true)->feed_key(interface_key::D_BUILDJOB);

    //df::coord pos = r->pos();
    //Gui::revealInDwarfmodeMap(pos, true);
    //Gui::setCursorCoords(pos.x, pos.y, pos.z);

    //Gui::getCurViewscreen(true)->feed_key(interface_key::CURSOR_LEFT);
    //if (toggle_restrict)
    //{
    //    Gui::getCurViewscreen(true)->feed_key(interface_key::BUILDJOB_DEPOT_BROKER_ONLY);
    //}
    //if (bld->trade_flags.bits.trader_requested != should_be_trading)
    //{
    //    Gui::getCurViewscreen(true)->feed_key(interface_key::BUILDJOB_DEPOT_REQUEST_TRADER);
    //    if (should_be_trading)
    //    {
    //        Gui::getCurViewscreen(true)->feed_key(interface_key::BUILDJOB_DEPOT_BRING);
    //        if (auto bring = virtual_cast<df::viewscreen_layer_assigntradest>(Gui::getCurViewscreen(true)))
    //        {
    //            ai.debug(out, stl_sprintf("[trade] Checking %zu possible trade items...", bring->lists[0].size()));
    //            for (size_t i = 0; i < bring->lists[0].size(); i++)
    //            {
    //                auto info = bring->info.at(bring->lists[0].at(i));
    //                if (info->unk) // TODO: determine if this field is really "banned by mandate".
    //                {
    //                    if (info->status == df::assign_trade_status::Pending)
    //                    {
    //                        info->status = df::assign_trade_status::RemovePending;
    //                    }
    //                    else if (info->status == df::assign_trade_status::Trading)
    //                    {
    //                        info->status = df::assign_trade_status::RemoveTrading;
    //                    }
    //                }
    //                else if (info->status == df::assign_trade_status::None && ai.stocks.willing_to_trade_item(out, info->item))
    //                {
    //                    info->status = df::assign_trade_status::AddPending;
    //                    ai.debug(out, "[trade] Bringing item to trade depot: " + AI::describe_item(info->item));
    //                }
    //            }
    //            Gui::getCurViewscreen(true)->feed_key(interface_key::LEAVESCREEN);
    //        }
    //    }
    //}
    Gui::getCurViewscreen(true)->feed_key(interface_key::LEAVESCREEN);

    ai.ignore_pause(start_x, start_y, start_z);

    return true;
}

class PerformTradeExclusive : public ExclusiveCallback
{
public:
    PerformTradeExclusive(AI & ai) : ExclusiveCallback{ "perform trade", 2 }, ai(ai)
    {
    }

private:
    AI & ai;

    std::vector<size_t> want_items;
    std::string want_qty;
    int32_t offer_value, request_value, max_offer_value;
    int32_t broker_item;
    std::string broker_qty;
    int32_t ten_percent;
    int32_t remove_item;
    std::string remove_qty;
    bool can_make_offer;

    virtual void Run(color_ostream & out);
    virtual ExclusiveCallback *ReplaceOnScreenChange()
    {
        if (can_make_offer)
        {
            ai.debug(Core::getInstance().getConsole(), "[trade] Unexpected viewscreen. Bailing.");
        }
        return new PerformTradeExclusive(ai);
    }

    void EnterCount(ExpectedScreen<df::viewscreen_tradegoodsst> & trade, const std::string & want);
    void ScrollTo(ExpectedScreen<df::viewscreen_tradegoodsst> & trade, bool right, int32_t *cursor, int32_t target);

    friend class Population;
};

bool Population::perform_trade(color_ostream & out)
{
    room *r = ai.find_room(room_type::tradedepot);
    if (!r)
    {
        return false;
    }
    df::building_tradedepotst *bld = virtual_cast<df::building_tradedepotst>(r->dfbuilding());
    if (!bld || bld->getBuildStage() < bld->getMaxBuildStage())
    {
        return false;
    }

    if (!AI::is_dwarfmode_viewscreen())
    {
        return false;
    }

    Gui::getViewCoords(trade_start_x, trade_start_y, trade_start_z);

    //Gui::getCurViewscreen(true)->feed_key(interface_key::D_BUILDJOB);

    //df::coord pos = r->pos();
    //Gui::revealInDwarfmodeMap(pos, true);
    //Gui::setCursorCoords(pos.x, pos.y, pos.z);

    //Gui::getCurViewscreen(true)->feed_key(interface_key::CURSOR_LEFT);
    //Gui::getCurViewscreen(true)->feed_key(interface_key::BUILDJOB_DEPOT_TRADE);
    //if (auto wait = virtual_cast<df::viewscreen_tradelistst>(Gui::getCurViewscreen(true)))
    //{
    //    if (wait->caravans.size() == 1)
    //    {
    //        wait->logic();
    //    }
    //    else
    //    {
    //        wait->feed_key(interface_key::SELECT);
    //    }
    //}
    //if (auto trade = virtual_cast<df::viewscreen_tradegoodsst>(Gui::getCurViewscreen(true)))
    //{
    //    if (events.register_exclusive(std::make_unique<PerformTradeExclusive>(ai)))
    //    {
    //        return false;
    //    }
    //    ai.debug(out, "[trade] Could not register exclusive context.");

    //    trade->feed_key(interface_key::LEAVESCREEN);
    //    Gui::getCurViewscreen(true)->feed_key(interface_key::LEAVESCREEN);
    //}
    //else
    //{
    //    ai.debug(out, "[trade] Opening the trade screen failed. Trying again soon.");
    //    Gui::getCurViewscreen(true)->feed_key(interface_key::LEAVESCREEN);
    //}

    ai.ignore_pause(trade_start_x, trade_start_y, trade_start_z);

    return false;
}

static bool represents_plant_murder(df::item *item)
{
    // TODO: other items, like soap and certain types of glass, and decorations and containers
    df::material *mat = MaterialInfo(item).material;
    return mat && mat->flags.is_set(material_flags::WOOD);
}

void PerformTradeExclusive::Run(color_ostream & out)
{
    //if (!MaybeExpectScreen<df::viewscreen_tradegoodsst>("")) // allow any type of tradegoods screen
    //{
    //    return;
    //}

    //ExpectedScreen<df::viewscreen_tradegoodsst> trade(this);

    //trade->render(); // make sure the item list is populated.

    //can_make_offer = true;

    //df::creature_raw *creature = trade->entity ? df::creature_raw::find(trade->entity->race) : nullptr;

    //if (trade->is_unloading)
    //{
    //    ai.debug(out, "[trade] Waiting for caravan to unload. Trying again soon.");

    //    Delay();

    //    can_make_offer = false;

    //    KeyNoDelay(interface_key::LEAVESCREEN);
    //    KeyNoDelay(interface_key::LEAVESCREEN);

    //    return;
    //}

    //ai.debug(out, "[trade] Scanning goods offered by " + trade->merchant_name + " from " + trade->merchant_entity + "...");

    //want_items.clear();
    //std::vector<df::item *> want_items_items;

    //for (auto it = trade->trader_items.begin(); it != trade->trader_items.end(); it++)
    //{
    //    if (ai.stocks.want_trader_item(out, *it, want_items_items))
    //    {
    //        want_items.push_back(it - trade->trader_items.begin());
    //        want_items_items.push_back(*it);
    //    }
    //}
    //std::sort(want_items.begin(), want_items.end(), [this, trade](size_t a, size_t b) -> bool
    //{
    //    return ai.stocks.want_trader_item_more(trade->trader_items.at(a), trade->trader_items.at(b));
    //});

    //max_offer_value = 0;

    //for (auto it : trade->broker_items)
    //{
    //    if (it->getType() == item_type::BIN)
    //    {
    //        continue;
    //    }
    //    if (trade->entity->entity_raw->ethic[ethic_type::KILL_PLANT] >= ethic_response::MISGUIDED && trade->entity->entity_raw->ethic[ethic_type::KILL_PLANT] <= ethic_response::UNTHINKABLE && represents_plant_murder(it))
    //    {
    //        continue;
    //    }
    //    if (trade->entity->entity_raw->ethic[ethic_type::KILL_ANIMAL] >= ethic_response::MISGUIDED && trade->entity->entity_raw->ethic[ethic_type::KILL_ANIMAL] <= ethic_response::UNTHINKABLE && it->isAnimalProduct())
    //    {
    //        continue;
    //    }
    //    max_offer_value += ai.trade.item_or_container_price_for_caravan(it, trade->caravan, trade->entity, creature, it->getStackSize(), trade->caravan->buy_prices, trade->caravan->sell_prices);
    //}

    //ai.debug(out, stl_sprintf("[trade] We have %d dorfbux of items we're willing to trade.", max_offer_value));

    //request_value = 0;
    //offer_value = 0;

    //auto want_items_it = want_items.begin();

    //while (want_items_it != want_items.end())
    //{
    //    df::item *item = trade->trader_items.at(*want_items_it);

    //    if ((request_value + ai.trade.item_or_container_price_for_caravan(item, trade->caravan, trade->entity, creature, 1, trade->caravan->buy_prices, trade->caravan->sell_prices)) * 11 / 10 >= max_offer_value)
    //    {
    //        ai.debug(out, "[trade] Cannot afford any of item, skipping: " + AI::describe_item(item));
    //        auto index = want_items_it - want_items.begin();
    //        want_items.erase(want_items_it);
    //        want_items_it = size_t(index) < want_items.size() ? want_items.begin() + index : want_items.end();

    //        continue;
    //    }

    //    ScrollTo(trade, false, &trade->trader_cursor, int32_t(*want_items_it));

    //    for (int32_t qty = item->getStackSize(); qty > 0; qty--)
    //    {
    //        int32_t item_value = ai.trade.item_or_container_price_for_caravan(item, trade->caravan, trade->entity, creature, qty, trade->caravan->buy_prices, trade->caravan->sell_prices);

    //        if ((request_value + item_value) * 11 / 10 >= max_offer_value)
    //        {
    //            if (qty > 1)
    //            {
    //                ai.debug(out, stl_sprintf("[trade] Cannot afford %d of item, trying again with fewer: ", qty) + AI::describe_item(item));
    //            }
    //            continue;
    //        }

    //        want_qty = stl_sprintf("%d", qty);
    //        request_value += item_value;
    //        ai.debug(out, stl_sprintf("[trade] Requesting %d of item: ", qty) + AI::describe_item(item));
    //        ai.debug(out, stl_sprintf("[trade] Requested: %d Offered: %d", request_value, offer_value));
    //        break;
    //    }

    //    EnterCount(trade, want_qty);

    //    while (request_value * 11 / 10 >= offer_value)
    //    {
    //        bool found = false;
    //        for (size_t i = 0; i < trade->broker_items.size(); i++)
    //        {
    //            if (trade->broker_items.at(i)->getType() == item_type::BIN)
    //            {
    //                continue;
    //            }
    //            if (trade->entity->entity_raw->ethic[ethic_type::KILL_PLANT] >= ethic_response::MISGUIDED && trade->entity->entity_raw->ethic[ethic_type::KILL_PLANT] <= ethic_response::UNTHINKABLE && represents_plant_murder(trade->broker_items.at(i)))
    //            {
    //                continue;
    //            }
    //            if (trade->entity->entity_raw->ethic[ethic_type::KILL_ANIMAL] >= ethic_response::MISGUIDED && trade->entity->entity_raw->ethic[ethic_type::KILL_ANIMAL] <= ethic_response::UNTHINKABLE && trade->broker_items.at(i)->isAnimalProduct())
    //            {
    //                continue;
    //            }
    //            int32_t current_count = trade->broker_selected.at(i) ? trade->broker_count.at(i) == 0 ? trade->broker_items.at(i)->getStackSize() : trade->broker_count.at(i) : 0;
    //            if (!current_count || current_count != trade->broker_items.at(i)->getStackSize())
    //            {
    //                auto offer_item = trade->broker_items.at(i);

    //                int32_t existing_offer_value = current_count ? ai.trade.item_or_container_price_for_caravan(offer_item, trade->caravan, trade->entity, creature, current_count, trade->caravan->buy_prices, trade->caravan->sell_prices) : 0;

    //                int32_t over_offer_qty = trade->broker_items.at(i)->getStackSize();
    //                for (int32_t offer_qty = over_offer_qty - 1; offer_qty > 0; offer_qty--)
    //                {
    //                    int32_t new_offer_value = ai.trade.item_or_container_price_for_caravan(offer_item, trade->caravan, trade->entity, creature, offer_qty, trade->caravan->buy_prices, trade->caravan->sell_prices);
    //                    if (offer_value - existing_offer_value + new_offer_value > request_value * 11 / 10)
    //                    {
    //                        over_offer_qty = offer_qty;
    //                    }
    //                    else
    //                    {
    //                        break;
    //                    }
    //                }

    //                int32_t new_offer_value = ai.trade.item_or_container_price_for_caravan(offer_item, trade->caravan, trade->entity, creature, over_offer_qty, trade->caravan->buy_prices, trade->caravan->sell_prices);
    //                offer_value = offer_value - existing_offer_value + new_offer_value;
    //                ai.debug(out, stl_sprintf("[trade] Offering %d%s of item: ", over_offer_qty - current_count, current_count ? " more" : "") + AI::describe_item(offer_item));
    //                ai.debug(out, stl_sprintf("[trade] Requested: %d Offered: %d", request_value, offer_value));

    //                broker_item = int32_t(i);
    //                broker_qty = stl_sprintf("%d", over_offer_qty);

    //                ScrollTo(trade, true, &trade->broker_cursor, broker_item);
    //                EnterCount(trade, broker_qty);

    //                found = true;

    //                break;
    //            }
    //        }

    //        if (!found)
    //        {
    //            Delay();
    //        }
    //    }

    //    want_items_it++;
    //}

    //while (can_make_offer)
    //{
    //    if (request_value <= 0 || offer_value <= 0)
    //    {
    //        ai.debug(out, "[trade] Cancelling trade.");
    //        can_make_offer = false;
    //        break;
    //    }

    //    ai.debug(out, "[trade] Making offer...");

    //    Key(interface_key::TRADE_TRADE);

    //    trade->logic();
    //    trade->render();

    //    std::string reply, mood;
    //    ai.trade.read_trader_reply(reply, mood);

    //    ai.debug(out, "[trade] Trader reply: " + reply);
    //    if (!mood.empty())
    //    {
    //        ai.debug(out, "[trade] Trader mood: " + mood);
    //    }

    //    if (!trade->counteroffer.empty())
    //    {
    //        for (auto it : trade->counteroffer)
    //        {
    //            ai.debug(out, "[trade] Trader requests item: " + AI::describe_item(it));
    //            offer_value += ai.trade.item_or_container_price_for_caravan(it, trade->caravan, trade->entity, creature, it->getStackSize(), trade->caravan->buy_prices, trade->caravan->sell_prices);
    //            ai.debug(out, stl_sprintf("[trade] Requested: %d Offered: %d", request_value, offer_value));
    //        }
    //        ai.debug(out, "[trade] Accepting counter-offer.");

    //        Key(interface_key::SELECT);
    //    }
    //    else
    //    {
    //        if (std::find_if(trade->broker_selected.begin(), trade->broker_selected.end(), [](int32_t count) -> bool { return count != 0; }) == trade->broker_selected.end())
    //        {
    //            ai.debug(out, "[trade] Offer was accepted.");
    //            can_make_offer = false;
    //            break;
    //        }

    //        if (!trade->has_traders)
    //        {
    //            ai.debug(out, "[trade] Trader no longer wants to trade. Giving up.");
    //            can_make_offer = false;
    //            break;
    //        }

    //        if (max_offer_value > offer_value * 6 / 5)
    //        {
    //            ten_percent = std::max(offer_value * 6 / 5 - offer_value, 1);
    //            ai.debug(out, stl_sprintf("[trade] Attempting to add %d dorfbux of offered goods...", ten_percent));

    //            while (ten_percent > 0)
    //            {
    //                for (size_t i = 0; i < trade->broker_items.size(); i++)
    //                {
    //                    if (trade->broker_items.at(i)->getType() == item_type::BIN)
    //                    {
    //                        continue;
    //                    }
    //                    if (trade->entity->entity_raw->ethic[ethic_type::KILL_PLANT] >= ethic_response::MISGUIDED && trade->entity->entity_raw->ethic[ethic_type::KILL_PLANT] <= ethic_response::UNTHINKABLE && represents_plant_murder(trade->broker_items.at(i)))
    //                    {
    //                        continue;
    //                    }
    //                    if (trade->entity->entity_raw->ethic[ethic_type::KILL_ANIMAL] >= ethic_response::MISGUIDED && trade->entity->entity_raw->ethic[ethic_type::KILL_ANIMAL] <= ethic_response::UNTHINKABLE && trade->broker_items.at(i)->isAnimalProduct())
    //                    {
    //                        continue;
    //                    }
    //                    int32_t current_count = trade->broker_selected.at(i) ? trade->broker_count.at(i) == 0 ? trade->broker_items.at(i)->getStackSize() : trade->broker_count.at(i) : 0;
    //                    if (!current_count || current_count != trade->broker_items.at(i)->getStackSize())
    //                    {
    //                        auto offer_item = trade->broker_items.at(i);

    //                        int32_t existing_offer_value = current_count ? ai.trade.item_or_container_price_for_caravan(offer_item, trade->caravan, trade->entity, creature, current_count, trade->caravan->buy_prices, trade->caravan->sell_prices) : 0;

    //                        int32_t over_offer_qty = trade->broker_items.at(i)->getStackSize();
    //                        for (int32_t offer_qty = over_offer_qty - 1; offer_qty > 0; offer_qty--)
    //                        {
    //                            int32_t new_offer_value = ai.trade.item_or_container_price_for_caravan(offer_item, trade->caravan, trade->entity, creature, offer_qty, trade->caravan->buy_prices, trade->caravan->sell_prices);
    //                            if (new_offer_value - existing_offer_value >= ten_percent)
    //                            {
    //                                over_offer_qty = offer_qty;
    //                            }
    //                            else
    //                            {
    //                                break;
    //                            }
    //                        }

    //                        int32_t new_offer_value = ai.trade.item_or_container_price_for_caravan(offer_item, trade->caravan, trade->entity, creature, over_offer_qty, trade->caravan->buy_prices, trade->caravan->sell_prices);
    //                        offer_value = offer_value - existing_offer_value + new_offer_value;
    //                        ten_percent = ten_percent + existing_offer_value - new_offer_value;
    //                        ai.debug(out, stl_sprintf("[trade] Offering %d%s of item: %s. %d dorfbux remain.", over_offer_qty - current_count, current_count ? " more" : "", AI::describe_item(offer_item).c_str(), ten_percent));

    //                        broker_item = int32_t(i);
    //                        broker_qty = stl_sprintf("%d", over_offer_qty);

    //                        break;
    //                    }
    //                }

    //                ScrollTo(trade, true, &trade->broker_cursor, broker_item);
    //                EnterCount(trade, broker_qty);
    //            }
    //        }
    //        else
    //        {
    //            ten_percent = std::max(request_value / 10, 1);
    //            ai.debug(out, stl_sprintf("[trade] Attempting to remove %d dorfbux of requested goods...", ten_percent));
    //            want_items_it = want_items.end();

    //            while (ten_percent > 0 && want_items_it != want_items.begin())
    //            {
    //                want_items_it--;

    //                df::item *item = trade->trader_items.at(*want_items_it);

    //                int32_t current_count = trade->trader_selected.at(*want_items_it) ? trade->trader_count.at(*want_items_it) == 0 ? trade->trader_items.at(*want_items_it)->getStackSize() : trade->trader_count.at(*want_items_it) : 0;

    //                if (current_count == 0)
    //                {
    //                    remove_item = -1;
    //                    break;
    //                }

    //                int32_t max_count = current_count;
    //                int32_t max_value = ai.trade.item_or_container_price_for_caravan(item, trade->caravan, trade->entity, creature, max_count, trade->caravan->buy_prices, trade->caravan->sell_prices);
    //                int32_t remove_count = max_count;
    //                int32_t remove_value = max_value;
    //                int32_t less_count = 0;
    //                int32_t less_value = 0;
    //                while (max_value - less_value > ten_percent && less_count < max_count)
    //                {
    //                    remove_count = max_count - less_count;
    //                    remove_value = max_value - less_value;
    //                    less_count++;
    //                    less_value = ai.trade.item_or_container_price_for_caravan(item, trade->caravan, trade->entity, creature, less_count, trade->caravan->buy_prices, trade->caravan->sell_prices);
    //                }

    //                remove_item = int32_t(*want_items_it);
    //                remove_qty = stl_sprintf("%d", current_count - remove_count);
    //                request_value -= remove_value;
    //                ten_percent -= remove_value;
    //                ai.debug(out, stl_sprintf("[trade] Removing %d of item: %s. %d dorfbux remain.", remove_count, AI::describe_item(item).c_str(), ten_percent));

    //                if (current_count == remove_count)
    //                {
    //                    want_items.erase(want_items_it);
    //                }
    //            }

    //            if (remove_item != -1)
    //            {
    //                ScrollTo(trade, false, &trade->trader_cursor, remove_item);
    //                EnterCount(trade, remove_qty);
    //            }
    //        }

    //        ai.debug(out, stl_sprintf("[trade] Requested: %d Offered: %d", request_value, offer_value));
    //    }
    //}

    //Delay();

    //KeyNoDelay(interface_key::LEAVESCREEN);
    //KeyNoDelay(interface_key::LEAVESCREEN);

    //ai.ignore_pause(ai.pop.trade_start_x, ai.pop.trade_start_y, ai.pop.trade_start_z);
    //ai.pop.did_trade = true;

    //if (ai.pop.set_up_trading(out, false))
    //{
    //    ai.debug(out, "[trade] Dismissed broker from depot: finished trade");
    //}
}

void PerformTradeExclusive::EnterCount(ExpectedScreen<df::viewscreen_tradegoodsst> & trade, const std::string & want)
{
    Key(interface_key::SELECT);

    //if (trade->in_edit_count)
    //{
    //    EnterString(&trade->edit_count, want);

    //    Key(interface_key::SELECT);
    //}
}

void PerformTradeExclusive::ScrollTo(ExpectedScreen<df::viewscreen_tradegoodsst> & trade, bool right, int32_t *cursor, int32_t target)
{
    //if (trade->in_right_pane != right)
    //{
    //    Key(right ? interface_key::STANDARDSCROLL_RIGHT : interface_key::STANDARDSCROLL_LEFT);
    //}

    MoveToItem(cursor, target);
}
