#include "ai.h"
#include "camera.h"
#include "hooks.h"
#include "debug.h"

#include <sstream>

#include "modules/Gui.h"
#include "modules/Maps.h"
#include "modules/Screen.h"
#include "modules/Units.h"

#include "df/activity_event_conflictst.h"
#include "df/creature_interaction_effect_body_transformationst.h"
#include "df/creature_raw.h"
#include "df/graphic.h"
#include "df/interfacest.h"
#include "df/job.h"
#include "df/syndrome.h"
//#include "df/plotinfo.h"
#include "df/unit.h"
#include "df/unit_syndrome.h"
#include "df/viewscreen_dwarfmodest.h"
//#include "df/viewscreen_movieplayerst.h"
#include "df/world.h"

REQUIRE_GLOBAL(gps);
REQUIRE_GLOBAL(gview);
REQUIRE_GLOBAL(pause_state);
REQUIRE_GLOBAL(plotinfo);
REQUIRE_GLOBAL(world);

Camera::Camera(AI & ai) :
    ai(ai),
    ontick_handle(nullptr),
    onupdate_handle(nullptr),
    onstatechange_handle(nullptr),
    following(-1),
    following_prev(),
    follow_unit(-1),
    follow_item(-1),
    follow_stop(true),
    movie_started_in_lockstep(false)
{
}

Camera::~Camera()
{
    events.onupdate_unregister(onupdate_handle);
    events.onstatechange_unregister(onstatechange_handle);
}

command_result Camera::startup(color_ostream &)
{
    return CR_OK;
}

command_result Camera::onupdate_register(color_ostream &)
{
    if (config.fps_meter)
    {
        gps->display_frames = 1;
    }
    ontick_handle = events.onupdate_register("df-ai camera (every tick)", 1, 1, [this](color_ostream& out) { update_tick(out); });
    onupdate_handle = events.onupdate_register("df-ai camera", 2000, 100, [this](color_ostream & out) { update(out); });
    onstatechange_handle = events.onstatechange_register("fps meter watcher", [this](color_ostream &, state_change_event mode)
    {
        if (config.fps_meter && mode == SC_VIEWSCREEN_CHANGED)
        {
            gps->display_frames = AI::is_dwarfmode_viewscreen() ? 1 : 0;
        }
    });
    check_record_status();
    return CR_OK;
}

void Camera::check_record_status()
{
    //if (config.record_movie && gview->supermovie_on == 0)
    //{
    //    movie_started_in_lockstep = lockstep_hooked;
    //    gview->supermovie_on = 1;
    //    gview->currentblocksize = 0;
    //    gview->nextfilepos = 0;
    //    gview->supermovie_pos = 0;
    //    gview->supermovie_delayrate = 0;
    //    gview->first_movie_write = 1;
    //    std::ostringstream filename;
    //    filename << "data/movies/df-ai-" << std::time(nullptr) << ".cmv";
    //    gview->movie_file = filename.str();
    //}
}

command_result Camera::onupdate_unregister(color_ostream &)
{
    if (config.fps_meter)
    {
        gps->display_frames = 0;
    }
    if (!config.no_quit && !config.random_embark)
    {
        Gui::getCurViewscreen(true)->breakdown_level = interface_breakdown_types::QUIT;
    }
    events.onupdate_unregister(ontick_handle);
    events.onupdate_unregister(onupdate_handle);
    events.onstatechange_unregister(onstatechange_handle);
    return CR_OK;
}

void Camera::update_tick(color_ostream &)
{
    if (plotinfo->follow_unit != -1 || plotinfo->follow_item != -1)
    {
        follow_stop = false;
        follow_unit = plotinfo->follow_unit;
        follow_item = plotinfo->follow_item;
    }
    else if (follow_stop)
    {
        follow_unit = -1;
        follow_item = -1;
    }
    else
    {
        follow_stop = true;
    }
}

void Camera::update(color_ostream &)
{
    if (!config.camera)
    {
        return;
    }

    if (following != plotinfo->follow_unit && !events.dfplex_client)
    {
        DFAI_DEBUG(camera, 1, "followed unit changed externally! was " << following << ", now " << plotinfo->follow_unit);
        following = plotinfo->follow_unit;
        return;
    }

    std::vector<df::unit *> targets0;
    std::vector<df::unit *> targets1;
    for (auto it = world->units.active.begin(); it != world->units.active.end(); it++)
    {
        df::unit *u = *it;
        df::tile_designation *td = Maps::getTileDesignation(Units::getPosition(u));
        if (u->flags1.bits.inactive || !td || td->bits.hidden)
            continue;
        df::creature_raw *race = df::creature_raw::find(u->race);
        if (race &&
            (race->flags.is_set(creature_raw_flags::HAS_ANY_MEGABEAST) ||
                race->flags.is_set(creature_raw_flags::HAS_ANY_SEMIMEGABEAST) ||
                race->flags.is_set(creature_raw_flags::HAS_ANY_FEATURE_BEAST) ||
                race->flags.is_set(creature_raw_flags::HAS_ANY_TITAN) ||
                race->flags.is_set(creature_raw_flags::HAS_ANY_UNIQUE_DEMON) ||
                race->flags.is_set(creature_raw_flags::HAS_ANY_DEMON) ||
                race->flags.is_set(creature_raw_flags::HAS_ANY_NIGHT_CREATURE)))
        {
            DFAI_DEBUG(camera, 4, "adding candidate: " << AI::describe_unit(u) << " (primary antagonist)");
            targets0.push_back(u);
        }
        else if (u->training_level != animal_training_level::Domesticated &&
            (u->flags1.bits.marauder ||
            u->flags1.bits.skeleton ||
            u->flags1.bits.active_invader ||
            u->flags2.bits.underworld ||
            u->flags2.bits.visitor_uninvited ||
            AI::is_in_conflict(u, [](df::activity_event_conflictst *c) -> bool
            {
                for (auto s : c->sides)
                {
                    for (auto e : s->enemies)
                    {
                        if (e->conflict_level > conflict_level::Encounter && e->conflict_level != conflict_level::Training)
                        {
                            return true;
                        }
                    }
                }
                return false;
            }) ||
            std::find_if(u->syndromes.active.begin(), u->syndromes.active.end(), [](df::unit_syndrome *us) -> bool
            {
                auto & s = df::syndrome::find(us->type)->ce;
                return std::find_if(s.begin(), s.end(), [](df::creature_interaction_effect *ce) -> bool
                {
                    return virtual_cast<df::creature_interaction_effect_body_transformationst>(ce) != nullptr;
                }) != s.end();
            }) != u->syndromes.active.end()))
        {
            DFAI_DEBUG(camera, 4, "adding candidate: " << AI::describe_unit(u) << " (conflict)");
            targets1.push_back(u);
        }
    }
    auto rnd_shuffle = [this](size_t n) -> size_t { return std::uniform_int_distribution<size_t>(0, n - 1)(ai.rng); };
    std::random_shuffle(targets0.begin(), targets0.end(), rnd_shuffle);
    std::random_shuffle(targets1.begin(), targets1.end(), rnd_shuffle);
    std::vector<df::unit *> targets2;
    for (auto it = world->units.active.begin(); it != world->units.active.end(); it++)
    {
        df::unit *u = *it;
        if (!u->flags1.bits.inactive && Units::isCitizen(u))
        {
            DFAI_DEBUG(camera, 5, "adding candidate: " << AI::describe_unit(u) << " (citizen)");
            targets2.push_back(u);
        }
    }
    std::random_shuffle(targets2.begin(), targets2.end(), rnd_shuffle);
    auto score = [](df::unit *u) -> int
    {
        if (!u->job.current_job)
        {
            return 0;
        }
        if (u->job.current_job->job_type == job_type::Sleep)
        {
            return 100;
        }
        switch (ENUM_ATTR(job_type, type, u->job.current_job->job_type))
        {
        case job_type_class::Misc:
            return -20;
        case job_type_class::Digging:
            return -50;
        case job_type_class::Building:
            return -35;
        case job_type_class::Hauling:
            return -40;
        case job_type_class::LifeSupport:
            return -10;
        case job_type_class::TidyUp:
            return -20;
        case job_type_class::Leisure:
            return -20;
        case job_type_class::Gathering:
            return -30;
        case job_type_class::Manufacture:
            return -10;
        case job_type_class::Improvement:
            return -10;
        case job_type_class::Crime:
            return -50;
        case job_type_class::LawEnforcement:
            return -30;
        case job_type_class::StrangeMood:
            return -20;
        case job_type_class::UnitHandling:
            return -30;
        case job_type_class::SiegeWeapon:
            return -50;
        case job_type_class::Medicine:
            return -70;
        }
        return 0;
    };
    std::sort(targets2.begin(), targets2.end(), [score](df::unit *a, df::unit *b) -> bool { return score(a) < score(b); });

    if (following != -1)
        following_prev.push_back(following);
    if (following_prev.size() > 3)
    {
        following_prev.erase(following_prev.begin(), following_prev.end() - 3);
    }

    size_t targets1_count = targets1.size();
    if (targets1_count > 3)
        targets1_count = 3;
    if (!targets2.empty())
    {
        for (auto it = targets1.begin(); it != targets1.end(); it++)
        {
            df::unit *u = *it;
            if (std::find(following_prev.begin(), following_prev.end(), u->id) == following_prev.end())
            {
                targets1_count--;
                if (targets1_count == 0)
                    break;
            }
        }
    }

    targets1.erase(targets1.begin(), targets1.begin() + targets1_count);
    targets0.insert(targets0.end(), targets1.begin(), targets1.end());
    targets0.insert(targets0.end(), targets2.begin(), targets2.end());

    df::unit *following_unit = nullptr;
    following = -1;
    if (!targets0.empty())
    {
        for (auto it = targets0.begin(); it != targets0.end(); it++)
        {
            df::unit *u = *it;
            if (std::find(following_prev.begin(), following_prev.end(), u->id) == following_prev.end() && !u->flags1.bits.caged)
            {
                following_unit = u;
                DFAI_DEBUG(camera, 1, "fallback: following candidate " << (it - targets0.begin()) << ": " << AI::describe_unit(following_unit));
                following = u->id;
                break;
            }
        }
        if (following == -1)
        {
            following_unit = targets0[std::uniform_int_distribution<size_t>(0, targets0.size() - 1)(ai.rng)];
            DFAI_DEBUG(camera, 1, "fallback: following unit " << AI::describe_unit(following_unit));
            following = following_unit->id;
        }
    }
    else
    {
        DFAI_DEBUG(camera, 1, "no good follow candidates");
    }

    if (following != -1 && !*pause_state)
    {
        Gui::revealInDwarfmodeMap(Units::getPosition(following_unit), true);
        plotinfo->follow_unit = following;
    }

    world->status.flags.bits.combat = 0;
    world->status.flags.bits.hunting = 0;
    world->status.flags.bits.sparring = 0;
}

void AI::ignore_pause(int32_t x, int32_t y, int32_t z)
{
    if (!config.camera)
    {
        Gui::setViewCoords(x, y, z);
        plotinfo->follow_unit = camera.follow_unit;
        plotinfo->follow_item = camera.follow_item;
        return;
    }

    if (df::unit *u = df::unit::find(camera.following))
    {
        Gui::revealInDwarfmodeMap(Units::getPosition(u), true);
        plotinfo->follow_unit = camera.following;
    }
}

std::string Camera::status()
{
    if (!config.camera)
    {
        return "disabled by config";
    }

    std::string fp;
    for (auto it = following_prev.begin(); it != following_prev.end(); it++)
    {
        if (!fp.empty())
        {
            fp += "; ";
        }
        fp += AI::describe_unit(df::unit::find(*it));
    }
    if (!fp.empty())
    {
        fp = " (previously: " + fp + ")";
    }
    if (following != -1 && (plotinfo->follow_unit == following || events.dfplex_client))
    {
        return "following " + AI::describe_unit(df::unit::find(following)) + fp;
    }
    return "inactive" + fp;
}
