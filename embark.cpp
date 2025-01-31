#include "ai.h"
#include "camera.h"
#include "embark.h"
#include "event_manager.h"
#include "hooks.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "LuaTools.h"
#pragma GCC diagnostic pop

#include "modules/Gui.h"
#include "modules/Screen.h"
#include "modules/Translation.h"

#include "df/caste_raw.h"
#include "df/creature_raw.h"
#include "df/embark_profile.h"
#include "df/item.h"
#include "df/region_map_entry.h"
#include "df/viewscreen_adopt_regionst.h"
#include "df/viewscreen_choose_start_sitest.h"
#include "df/viewscreen_dwarfmodest.h"
#include "df/viewscreen_export_regionst.h"
#include "df/viewscreen_game_cleanerst.h"
#include "df/viewscreen_loadgamest.h"
//#include "df/viewscreen_movieplayerst.h"
#include "df/viewscreen_new_regionst.h"
#include "df/viewscreen_setupdwarfgamest.h"
//#include "df/viewscreen_textviewerst.h"
#include "df/viewscreen_titlest.h"
#include "df/viewscreen_update_regionst.h"
#include "df/world.h"
#include "df/world_data.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(standing_orders_gather_refuse_outside);
REQUIRE_GLOBAL(standing_orders_job_cancel_announce);
REQUIRE_GLOBAL(world);

EmbarkExclusive::EmbarkExclusive(AI & ai) :
    ExclusiveCallback{ "embarking" },
    ai(ai)
{
}

EmbarkExclusive::~EmbarkExclusive()
{
}

void EmbarkExclusive::Run(color_ostream & out)
{
    while (!MaybeExpectScreen<df::viewscreen_dwarfmodest>("dwarfmode/Default"))
    {
        AssertDelayed();

        if (Screen::isDismissed(Gui::getCurViewscreen(false)))
        {
            Delay();
            continue;
        }

        //if (MaybeExpectScreen<df::viewscreen_movieplayerst>("movieplayer"))
        //{
        //    Key(interface_key::LEAVESCREEN);
        //    continue;
        //}

        if (MaybeExpectScreen<df::viewscreen_titlest>("title"))
        {
            ViewTitle(out);
            continue;
        }

        if (MaybeExpectScreen<df::viewscreen_adopt_regionst>("adopt_region") || MaybeExpectScreen<df::viewscreen_export_regionst>("export_region"))
        {
            Delay();
            continue;
        }

        if (MaybeExpectScreen<df::viewscreen_loadgamest>("loadgame"))
        {
            ViewLoadGame(out);
            continue;
        }

        if (MaybeExpectScreen<dfhack_lua_viewscreen>("dfhack/lua/load_screen", "loadgame"))
        {
            ViewLoadScreen(out);
            continue;
        }

        // TODO: get a real focus string for the load_screen_options dialog
        if (MaybeExpectScreen<dfhack_lua_viewscreen>("dfhack/lua", "dfhack/lua/load_screen"))
        {
            ViewLoadScreenOptions();
            continue;
        }

        if (MaybeExpectScreen<df::viewscreen_new_regionst>("new_region"))
        {
            ViewNewRegion(out);
            continue;
        }

        if (MaybeExpectScreen<df::viewscreen_update_regionst>("update_region"))
        {
            ViewUpdateRegion(out);
            continue;
        }

        if (MaybeExpectScreen<df::viewscreen_choose_start_sitest>("choose_start_site"))
        {
            ViewChooseStartSite(out);
            continue;
        }

        if (MaybeExpectScreen<df::viewscreen_setupdwarfgamest>("setupdwarfgame"))
        {
            ViewSetupDwarfGame(out);
            continue;
        }

        //if (MaybeExpectScreen<df::viewscreen_textviewerst>("textviewer"))
        //{
        //    ViewTextViewer(out);
        //    return;
        //}

        // viewscreen is unknown
        ai.statechanged(out, SC_VIEWSCREEN_CHANGED);
        Delay();
    }

    ai.unpause();
}

void EmbarkExclusive::SelectVerticalMenuItem(int32_t *current, int32_t target)
{
    MoveToItem(current, target);

    Key(interface_key::SELECT);
}

void EmbarkExclusive::SelectHorizontalMenuItem(int32_t *current, int32_t target)
{
    //MoveToItem(current, target, interface_key::STANDARDSCROLL_RIGHT, interface_key::STANDARDSCROLL_LEFT);

    Key(interface_key::SELECT);
}

void EmbarkExclusive::ViewTitle(color_ostream & out)
{
    ExpectedScreen<df::viewscreen_titlest> view(this);

    ai.camera.check_record_status();

    //if (view->sel_subpage == df::viewscreen_titlest::None)
    //{
    //    auto continue_game = std::find(view->menu_line_id.begin(), view->menu_line_id.end(), df::viewscreen_titlest::Continue);

    //    if (!config.random_embark_world.empty() && continue_game != view->menu_line_id.end() && std::ifstream("data/save/" + config.random_embark_world + "/world.sav").good())
    //    {
    //        ai.debug(out, "choosing \"Continue Game\"");

    //        SelectVerticalMenuItem(&view->sel_menu_line, int32_t(continue_game - view->menu_line_id.begin()));

    //        return;
    //    }

    //    auto start_game = std::find(view->menu_line_id.begin(), view->menu_line_id.end(), df::viewscreen_titlest::Start);

    //    if (!config.random_embark_world.empty() && start_game != view->menu_line_id.end() && std::ifstream("data/save/" + config.random_embark_world + "/world.dat").good())
    //    {
    //        ai.debug(out, "choosing \"Start Game\"");

    //        SelectVerticalMenuItem(&view->sel_menu_line, int32_t(start_game - view->menu_line_id.begin()));

    //        return;
    //    }

    //    auto new_world = std::find(view->menu_line_id.begin(), view->menu_line_id.end(), df::viewscreen_titlest::NewWorld);

    //    ai.debug(out, "choosing \"New World\"");

    //    //SelectVerticalMenuItem(&view->sel_menu_line, int32_t(new_world - view->menu_line_id.begin()));
    //}

    //if (view->sel_subpage == df::viewscreen_titlest::StartSelectWorld)
    //{
    //    if (config.random_embark_world.empty())
    //    {
    //        ai.debug(out, "leaving \"Select World\" (no save name)");

    //        Key(interface_key::LEAVESCREEN);

    //        return;
    //    }

    //    auto save = std::find_if(view->start_savegames.begin(), view->start_savegames.end(), [](df::viewscreen_titlest::T_start_savegames *s) -> bool
    //    {
    //        return s->save_dir == config.random_embark_world;
    //    });

    //    if (save == view->start_savegames.end())
    //    {
    //        ai.debug(out, "could not find a save named " + config.random_embark_world);
    //        config.set(out, config.random_embark_world, std::string());

    //        Key(interface_key::LEAVESCREEN);

    //        return;
    //    }
    //    else
    //    {
    //        ai.debug(out, stl_sprintf("selecting save #%d (%s)",
    //            int((save - view->start_savegames.begin()) + 1),
    //            (*save)->world_name_str.c_str()));

    //        SelectVerticalMenuItem(&view->sel_submenu_line, int32_t(save - view->start_savegames.begin()));
    //    }
    //}

   /* if (view->sel_subpage == df::viewscreen_titlest::StartSelectMode)
    {
        auto fortress_mode = std::find(view->submenu_line_id.begin(), view->submenu_line_id.end(), 0);
        if (fortress_mode == view->submenu_line_id.end())
        {
            ai.debug(out, "leaving \"Select Mode\" (no fortress mode available)");
            config.set(out, config.random_embark_world, std::string());

            Key(interface_key::LEAVESCREEN);

            return;
        }

        ai.debug(out, "choosing \"Dwarf Fortress Mode\"");

        SelectVerticalMenuItem(&view->sel_menu_line, int32_t(fortress_mode - view->submenu_line_id.begin()));
    }*/

#undef view
}

void EmbarkExclusive::ViewLoadGame(color_ostream & out)
{
    ExpectedScreen<df::viewscreen_loadgamest> view(this);

    if (view->loading)
    {
        Delay();

        return;
    }

    if (config.random_embark_world.empty())
    {
        ai.debug(out, "leaving \"Select World\" (no save name)");

        Key(interface_key::LEAVESCREEN);

        return;
    }

    //auto save = std::find_if(view->saves.begin(), view->saves.end(), [](df::loadgame_save_info *s) -> bool
    //{
    //    return s->folder_name == config.random_embark_world;
    //});

    /*if (save == view->saves.end())
    {
        ai.debug(out, "could not find save named " + config.random_embark_world);
        config.set(out, config.random_embark_world, std::string());

        Key(interface_key::LEAVESCREEN);

        return;
    }

    ai.debug(out, stl_sprintf("selecting save #%d (%s) (%s)",
        int((save - view->saves.begin()) + 1),
        (*save)->world_name.c_str(),
        (*save)->fort_name.c_str()));

    SelectVerticalMenuItem(&view->sel_idx, int32_t(save - view->saves.begin()));*/
}

void EmbarkExclusive::ViewLoadScreen(color_ostream & out)
{
    ExpectedScreen<dfhack_lua_viewscreen> view(this);

    if (config.random_embark_world.empty())
    {
        ai.debug(out, "leaving \"Select World\" (no save name)");

        Key(interface_key::LEAVESCREEN);

        return;
    }

    std::vector<df::loadgame_save_info *> filtered_saves;
    auto parent = strict_virtual_cast<df::viewscreen_loadgamest>(view->parent);
    //for (auto save : parent->saves)
    //{
    //    size_t len = save->folder_name.length();
    //    const static std::string tmpl("-#####-##-##");
    //    if (len < tmpl.length())
    //    {
    //        filtered_saves.push_back(save);
    //    }
    //    else
    //    {
    //        for (size_t i = 0; i < tmpl.length(); i++)
    //        {
    //            char expect = tmpl.at(tmpl.length() - i - 1);
    //            char actual = save->folder_name.at(len - i - 1);
    //            if (expect == '#' ? actual < '0' || actual > '9' : actual != expect)
    //            {
    //                filtered_saves.push_back(save);
    //                break;
    //            }
    //        }
    //    }
    //}

    auto save = std::find_if(filtered_saves.begin(), filtered_saves.end(), [](df::loadgame_save_info *s) -> bool
    {
        return s->folder_name == config.random_embark_world;
    });

    if (save == filtered_saves.end())
    {
        ai.debug(out, "could not find save named " + config.random_embark_world);
        config.set(out, config.random_embark_world, std::string());

        Key(interface_key::LEAVESCREEN);

        return;
    }

    auto L = Lua::Core::State;
    lua_rawgetp(L, LUA_REGISTRYINDEX, view.get());
    lua_getfield(L, -1, "sel_idx");
    int32_t sel_idx = static_cast<int32_t>(lua_tointeger(L, -1)) - 1;
    lua_pop(L, 2);

    ai.debug(out, stl_sprintf("selecting save #%d (%s) (%s)",
        int((save - filtered_saves.begin()) + 1),
        (*save)->world_name.c_str(),
        (*save)->fort_name.c_str()));

    int32_t target(save - filtered_saves.begin());

    while (sel_idx != target)
    {
        if (sel_idx < target)
        {
            Key(interface_key::STANDARDSCROLL_DOWN);
            sel_idx++;
        }
        else
        {
            Key(interface_key::STANDARDSCROLL_UP);
            sel_idx--;
        }
    }

    Key(interface_key::SELECT);
}

void EmbarkExclusive::ViewLoadScreenOptions()
{
    ExpectedScreen<dfhack_lua_viewscreen> view(this);

    auto L = Lua::Core::State;
    lua_rawgetp(L, LUA_REGISTRYINDEX, view.get());
    lua_getfield(L, -1, "save_mtime");
    if (lua_isnoneornil(L, -1))
    {
        // not on load_screen_options
        lua_pop(L, 2);
        return;
    }
    lua_getfield(L, -2, "loading");
    bool startedLoading = !!lua_isboolean(L, -1);
    bool finishedLoading = startedLoading && !lua_toboolean(L, -1);
    lua_pop(L, 3);

    if (!startedLoading)
    {
        Key(interface_key::SELECT);
    }
    else if (!finishedLoading)
    {
        Delay();
    }
}

void EmbarkExclusive::ViewNewRegion(color_ostream & out)
{
    ExpectedScreen<df::viewscreen_new_regionst> view(this);

    config.set(out, config.random_embark_world, std::string());

    //while (view->load_world_params)
    //{
    //    // wait for screen to initialize
    //    Delay();
    //}

    //if (!view->welcome_msg.empty())
    //{
    //    ai.debug(out, "leaving world gen disclaimer");

    //    Key(interface_key::LEAVESCREEN);

    //    return;
    //}

    //if (view->simple_mode == 1)
    //{
    //    ai.debug(out, "choosing \"Generate World\"");

    //    int32_t want_size = std::min(std::max(config.world_size, 0), 4);

    //    if (view->world_size != want_size)
    //    {
    //        while (view->cursor_line != 0)
    //        {
    //            Key(interface_key::STANDARDSCROLL_UP);
    //        }

    //        SelectHorizontalMenuItem(&view->world_size, want_size);
    //    }

    //    int32_t want_minerals = 3;

    //    if (view->mineral_occurence != want_minerals)
    //    {
    //        while (view->cursor_line != 6)
    //        {
    //            Key(interface_key::STANDARDSCROLL_DOWN);
    //        }

    //        SelectHorizontalMenuItem(&view->mineral_occurence, want_minerals);
    //    }

    //    Key(interface_key::MENU_CONFIRM);

    //    return;
    //}

    //if (!world->entities.all.empty() && view->simple_mode == 0 && world->worldgen_status.state == 10)
    //{
    //    ai.debug(out, "world gen finished, save name is " + world->cur_savegame.save_dir);
    //    config.set(out, config.random_embark_world, world->cur_savegame.save_dir);

    //    Key(interface_key::SELECT);

    //    return;
    //}

    Delay();
}

void EmbarkExclusive::ViewUpdateRegion(color_ostream & out)
{
    ExpectedScreen<df::viewscreen_update_regionst> view(this);

    ai.debug(out, "updating world, goal: " + AI::timestamp(view->year, view->year_tick));
    Delay();
}

void EmbarkExclusive::ViewChooseStartSite(color_ostream & out)
{
    ExpectedScreen<df::viewscreen_choose_start_sitest> view(this);

    bool no_preference = true;

    FOR_ENUM_ITEMS(embark_finder_option, o)
    {
        if (o == embark_finder_option::DimensionX || o == embark_finder_option::DimensionY)
        {
            continue;
        }

        if (config.embark_options[o] != -1)
        {
            no_preference = false;
            break;
        }
    }

    if (no_preference)
    {
        ai.debug(out, "no embark preferences; skipping site finder");

        DisplayEmbarkSite(out);

        return;
    }

    //if (view->finder.finder_state == -1)
    //{
    //    ai.debug(out, "choosing \"Site Finder\"");

    //    Key(interface_key::SETUP_FIND);

    //    FOR_ENUM_ITEMS(embark_finder_option, o)
    //    {
    //        if (view->finder.options[o] == config.embark_options[o])
    //        {
    //            continue;
    //        }

    //        auto visible = std::find(view->finder.visible_options.begin(), view->finder.visible_options.end(), o);

    //        if (visible == view->finder.visible_options.end())
    //        {
    //            ai.debug(out, "[CHEAT] Setting hidden site finder option " + enum_item_key(o));
    //            view->finder.options[o] = config.embark_options[o];

    //            continue;
    //        }

    //        MoveToItem(&view->finder.cursor, int32_t(visible - view->finder.visible_options.begin()));

    //        if (o == embark_finder_option::DimensionX || o == embark_finder_option::DimensionY)
    //        {
    //            int32_t target = std::min(std::max(config.embark_options[o], 1), 16);
    //            MoveToItem(&view->finder.options[o], target, interface_key::STANDARDSCROLL_RIGHT, interface_key::STANDARDSCROLL_LEFT);
    //            continue;
    //        }

    //        if (config.embark_options[o] == -1)
    //        {
    //            while (view->finder.options[o] != -1)
    //            {
    //                Key(interface_key::STANDARDSCROLL_LEFT);
    //            }

    //            continue;
    //        }

    //        if (view->finder.options[o] == -1)
    //        {
    //            Key(interface_key::STANDARDSCROLL_RIGHT);
    //        }

    //        while (view->finder.options[o] != -1 && view->finder.options[o] != config.embark_options[o])
    //        {
    //            if (view->finder.options[o] > config.embark_options[o])
    //            {
    //                Key(interface_key::STANDARDSCROLL_LEFT);
    //            }
    //            else
    //            {
    //                Key(interface_key::STANDARDSCROLL_RIGHT);
    //            }
    //        }
    //    }

    //    Key(interface_key::SELECT);

    //    return;
    //}

    //while (view->finder.search_x != -1 || view->finder.search_y != 0)
    //{
    //    ai.debug(out, stl_sprintf("searching for a site (%d/%d, %d/%d)",
    //        view->finder.search_x,
    //        world->world_data->world_width / 16,
    //        view->finder.search_y,
    //        world->world_data->world_height / 16));

    //    Delay();
    //}

    ai.debug(out, "choosing \"Embark\"");

    Key(interface_key::LEAVESCREEN);

    df::coord2d start = view->location.region_pos;
    std::vector<df::coord2d> sites;
    for (int16_t x = 0; x < world->world_data->world_width; x++)
    {
        for (int16_t y = 0; y < world->world_data->world_height; y++)
        {
            if (world->world_data->region_map[x][y].finder_rank >= 10000)
            {
                sites.push_back(df::coord2d(x, y));
            }
        }
    }
    if (sites.empty())
    {
        ai.debug(out, "leaving embark selector (no good embarks)");
        config.set(out, config.random_embark_world, std::string());
        AI::abandon(out);
        return;
    }

    ai.debug(out, stl_sprintf("found sites count: %zu", sites.size()));
    // Don't embark on the same region every time.
    std::vector<std::seed_seq::result_type> seeds;
    seeds.push_back(std::seed_seq::result_type(ai.rng()));
    seeds.push_back(std::seed_seq::result_type(*cur_year));
    seeds.push_back(std::seed_seq::result_type(*cur_year_tick));
    std::seed_seq seeds_seq(seeds.begin(), seeds.end());
    std::mt19937 rng(seeds_seq);
    df::coord2d site = sites[std::uniform_int_distribution<size_t>(0, sites.size() - 1)(rng)];
    df::coord2d selected_site_diff = site - start;

    while (selected_site_diff.x > 0)
    {
        selected_site_diff.x--;

        Key(interface_key::CURSOR_RIGHT);
    }

    while (selected_site_diff.x < 0)
    {
        selected_site_diff.x++;

        Key(interface_key::CURSOR_LEFT);
    }

    while (selected_site_diff.y > 0)
    {
        selected_site_diff.y--;

        Key(interface_key::CURSOR_DOWN);
    }

    while (selected_site_diff.y < 0)
    {
        selected_site_diff.y++;

        Key(interface_key::CURSOR_UP);
    }

    //Key(interface_key::SETUP_FIND);

    DisplayEmbarkSite(out);
}

void EmbarkExclusive::DisplayEmbarkSite(color_ostream &)
{
    ExpectedScreen<df::viewscreen_choose_start_sitest> view(this);

    Delay(5 * 100);

    //Key(interface_key::CHANGETAB);

    //while (view->page != df::viewscreen_choose_start_sitest::Biome)
    //{
    //    Delay(5 * 100);

    //    Key(interface_key::CHANGETAB);
    //}

    //Delay(5 * 100);

    //Key(interface_key::SETUP_BIOME_1);

    //while (view->biome_highlighted)
    //{
    //    int32_t biome_idx = view->biome_idx + 1;
    //    Key(static_cast<df::interface_key>(interface_key::SETUP_BIOME_1 + biome_idx));
    //    if (view->biome_highlighted && biome_idx == view->biome_idx)
    //    {
    //        Delay(5 * 100);
    //    }
    //    else
    //    {
    //        break;
    //    }
    //}

    //Key(interface_key::SETUP_EMBARK);

    // dismiss warnings
    Key(interface_key::SELECT);
}

void EmbarkExclusive::ViewSetupDwarfGame(color_ostream & out)
{
    ExpectedScreen<df::viewscreen_setupdwarfgamest> view(this);

    //if (view->embark_prompt)
    //{
    //    Key(interface_key::SELECT);

    //    return;
    //}

    //if (view->show_play_now)
    //{
    //    size_t choice = std::uniform_int_distribution<size_t>(0, view->choices.size() - 1)(ai.rng);

    //    switch (view->choice_types.at(choice))
    //    {
    //    case df::viewscreen_setupdwarfgamest::PlayNow:
    //        ai.debug(out, "choosing \"Play Now\"");
    //        break;
    //    case df::viewscreen_setupdwarfgamest::Prepare:
    //        ai.debug(out, "choosing \"Prepare Carefully\"");
    //        break;
    //    case df::viewscreen_setupdwarfgamest::Profile:
    //        ai.debug(out, "choosing embark profile: \"" + view->choices.at(choice)->name + "\"");
    //        break;
    //    }

    //    while (view->choice != int32_t(choice))
    //    {
    //        Key(interface_key::STANDARDSCROLL_DOWN);
    //    }

    //    Key(interface_key::SELECT);

    //    return;
    //}

    //// TODO: more interesting custom loadout generation

    //Key(interface_key::CHANGETAB);
    //Key(interface_key::STANDARDSCROLL_RIGHT);

    //auto add_animal = [&](const std::string& name, const std::string& caste_name, int32_t amount)
    //{
    //    int32_t start = view->animal_cursor;
    //    for (;;)
    //    {
    //        auto creature = df::creature_raw::find(view->animals.race.at(view->animal_cursor));
    //        auto caste = creature->caste.at(view->animals.caste.at(view->animal_cursor));
    //        if (creature->creature_id != name || caste->caste_id != caste_name || view->animals.profession.at(view->animal_cursor) != profession::STANDARD)
    //        {
    //            Key(interface_key::STANDARDSCROLL_UP);
    //            if (view->animal_cursor == start)
    //            {
    //                ai.debug(out, "[ERROR] Could not find creature " + name + ":" + caste_name + " on embark menu.");
    //                return;
    //            }
    //            continue;
    //        }

    //        for (int32_t i = 0; i < amount; i++)
    //        {
    //            Key(interface_key::SECONDSCROLL_DOWN);
    //        }

    //        return;
    //    }
    //};

    //add_animal("CAT", "MALE", 1);
    //add_animal("CAT", "FEMALE", 2);
    //add_animal("DOG", "MALE", 1);
    //add_animal("DOG", "FEMALE", 2);
    //add_animal("BIRD_TURKEY", "MALE", 2);
    //add_animal("BIRD_TURKEY", "FEMALE", 3);
    //add_animal("PIG", "MALE", 1);
    //add_animal("PIG", "FEMALE", 2);

    //Key(interface_key::STANDARDSCROLL_LEFT);
    //for (size_t i = 0; i < view->items.size(); i++)
    //{
    //    auto item = view->items.at(i);
    //    if (item->getType() != item_type::WEAPON || item->getMeleeSkill() != job_skill::MINING)
    //    {
    //        continue;
    //    }

    //    while (view->item_cursor != int32_t(i))
    //    {
    //        Key(interface_key::STANDARDSCROLL_DOWN);
    //    }

    //    Key(interface_key::SECONDSCROLL_DOWN);
    //    break;
    //}
    //for (size_t i = 0; i < view->items.size(); i++)
    //{
    //    auto item = view->items.at(i);
    //    if (item->getType() != item_type::BUCKET)
    //    {
    //        continue;
    //    }

    //    while (view->item_cursor != int32_t(i))
    //    {
    //        Key(interface_key::STANDARDSCROLL_DOWN);
    //    }

    //    Key(interface_key::SECONDSCROLL_DOWN);
    //    Key(interface_key::SECONDSCROLL_DOWN);
    //    break;
    //}

    //Key(interface_key::SETUP_EMBARK);
}

void EmbarkExclusive::ViewTextViewer(color_ostream & out)
{
    ai.debug(out, "site is ready.");

    Delay(5 * 100);

    ai.debug(out, "disabling minimap.");
    Gui::getCurViewscreen(true)->feed_key(interface_key::LEAVESCREEN);
    //Gui::setMenuWidth(3, 3);
    *standing_orders_gather_refuse_outside = 1;
    *standing_orders_job_cancel_announce = config.cancel_announce;
}

RestartWaitExclusive::RestartWaitExclusive(AI & ai) :
    ExclusiveCallback("restart wait"),
    ai(ai)
{
}

RestartWaitExclusive::~RestartWaitExclusive()
{
}

void RestartWaitExclusive::Run(color_ostream & out)
{
    //ExpectScreen<df::viewscreen_textviewerst>("textviewer");

    //ai.debug(out, "game over. restarting in 1 minute.");

    //Delay(60 * 100);

    //if (!MaybeExpectScreen<df::viewscreen_textviewerst>("textviewer"))
    //{
    //    ai.debug(out, "[ERROR] unexpected screen during restart.");
    //    return;
    //}

    //ai.debug(out, "restarting.");

    //Key(interface_key::LEAVESCREEN);

    //while (!MaybeExpectScreen<df::viewscreen_titlest>("title"))
    //{
    //    Delay();
    //}

    //if (!config.no_quit)
    //{
    //    if (config.lockstep)
    //    {
    //        Hook_Shutdown_Now();
    //    }
    //    else
    //    {
    //        Gui::getCurViewscreen(true)->breakdown_level = interface_breakdown_types::QUIT;
    //    }
    //}
    //else
    //{
    //    extern bool full_reset_requested;
    //    full_reset_requested = true;
    //}
}
