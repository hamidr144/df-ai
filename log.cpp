#include "ai.h"
#include "camera.h"
#include "embark.h"
#include "plan.h"
#include "population.h"

#include "modules/Gui.h"
#include "modules/Translation.h"
#include "modules/Units.h"

#include "df/activity_entry.h"
#include "df/activity_event.h"
#include "df/activity_event_participants.h"
#include "df/history_event.h"
#include "df/history_event_context.h"
#include "df/interface_button_building_new_jobst.h"
#include "df/item.h"
#include "df/job.h"
#include "df/manager_order.h"
#include "df/manager_order_template.h"
#include "df/report.h"
#include "df/world.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(world);

std::string AI::timestamp(int32_t y, int32_t t)
{
    if (y == 0 && t == 0)
    {
        // split up to avoid trigraphs
        return "?????" "-" "??" "-" "??" ":" "????";
    }
    if (t == -1)
    {
        // split up to avoid trigraphs
        return stl_sprintf("%05d-" "??" "-" "??" ":" "????", y);
    }
    return stl_sprintf("%05d-%02d-%02d:%04d",
        y,                    // year
        t / 50 / 24 / 28 + 1, // month
        t / 50 / 24 % 28 + 1, // day
        t % (24 * 50));       // time
}

std::string AI::timestamp()
{
    return timestamp(*cur_year, *cur_year_tick);
}

std::string AI::describe_item(df::item *i)
{
    if (!i)
    {
        return "(unknown item)";
    }

    std::string s;
    i->getItemDescription(&s, 0);
    return s;
}

std::string AI::describe_name(const df::language_name & name, bool in_english, bool only_last_part)
{
    std::string s = Translation::TranslateName(&name, in_english, only_last_part);
    return Translation::capitalize(s);
}

std::string AI::describe_unit(df::unit *u, bool html)
{
    // Unknown Unit
    if (!u)
    {
        if (html)
        {
            return "<i>(unknown unit)</i>";
        }
        return "(unknown unit)";
    }

    // Name
    std::string s = describe_name(u->name);
    if (!s.empty())
    {
        s += ", ";
    }

    // Curse Name
    if (!u->curse.name.empty())
    {
        s += u->curse.name;
        s += " ";
    }

    // Profession Name
    s += Units::getProfessionName(u);
    if (html)
    {
        s = html_escape(s);
        if (u->hist_figure_id != -1)
        {
            s = stl_sprintf("<a href=\"fig-%d\">", u->hist_figure_id) + s + "</a>";
        }
    }
    return s;
}

template<typename T>
static std::string do_describe_job(T *job)
{
    if (!job)
    {
        return "(unknown job)";
    }

    std::string desc;
    auto button = df::allocate<df::interface_button_building_new_jobst>();
    //button->reaction_name = job->reaction_name;
    //button->hist_figure_id = job->hist_figure_id;
    //button->job_type = job->job_type;
    //button->item_type = job->item_type;
    //button->item_subtype = job->item_subtype;
    //button->mat_type = job->mat_type;
    //button->mat_index = job->mat_index;
    //button->item_category = job->item_category;
    //button->material_category = job->material_category;

    //button->getLabel(&desc);
    delete button;

    return desc;
}

std::string AI::describe_job(const df::job *job)
{
    return do_describe_job(job);
}

std::string AI::describe_job(const df::manager_order *job)
{
    return do_describe_job(job);
}

std::string AI::describe_job(const df::manager_order_template *job)
{
    return do_describe_job(job);
}

std::string AI::describe_job(const df::unit *u)
{
    if (u->job.current_job != nullptr)
    {
        return do_describe_job(u->job.current_job);
    }

    std::string s;
    for (auto act : world->activities.all)
    {
        for (auto e : act->events)
        {
            if (auto p = e->getParticipantInfo())
            {
                if (std::find(p->units.begin(), p->units.end(), u->id) != p->units.end())
                {
                    std::string name;
                    e->getName(u->id, &name);
                    if (!s.empty())
                    {
                        s += " / ";
                    }
                    s += name;
                }
            }
        }
    }
    return s;
}

std::string AI::describe_event(df::history_event *event)
{
    if (!event)
    {
        return "(unknown event)";
    }

    df::history_event_context context;
    std::string str;
    event->getSentence(&str, &context);
    return str;
}

void AI::write_df(std::ostream & out, const std::string & str, const std::string & newline, const std::string & suffix, std::function<std::string(const std::string &)> translate)
{
    size_t pos = 0;
    for (;;)
    {
        size_t end = str.find('\n', pos);
        if (end == std::string::npos)
        {
            out << translate(str.substr(pos)) << suffix;
            break;
        }
        out << translate(str.substr(pos, end - pos)) << newline;
        pos = end + 1;
    }
    out.flush();
}

void AI::write_lockstep(std::string str, uint8_t color)
{
    int32_t lines = 1;
    size_t pos = 0;

    for (;;)
    {
        size_t end = str.find('\n', pos);
        if (end == std::string::npos)
        {
            while (pos + 80 <= str.size())
            {
                lines++;
                pos += 80;
            }
            break;
        }
        while (pos + 80 <= end)
        {
            lines++;
            pos += 80;
        }
        lines++;
        pos = end + 1;
    }

    while (lines > 25)
    {
        pos = str.find('\n');
        if (pos >= 80)
        {
            pos = 79;
        }
        str = str.substr(0, pos + 1);
        lines--;
    }

    for (size_t src = lines, dst = 0; src < 25; src++, dst++)
    {
        memcpy(lockstep_log_buffer[dst], lockstep_log_buffer[src], 80);
        lockstep_log_color[dst] = lockstep_log_color[src];
    }

    for (size_t i = 0, y = 25 - lines; y < 25; y++)
    {
        for (size_t x = 0; x < 80; x++)
        {
            if (i >= str.size() || str.at(i) == '\n')
            {
                lockstep_log_buffer[y][x] = ' ';
            }
            else
            {
                lockstep_log_buffer[y][x] = str.at(i);
                i++;
            }
        }
        if (i < str.size() && str.at(i) == '\n')
        {
            i++;
        }
        lockstep_log_color[y] = color;
    }
}

void AI::debug(color_ostream & out, const std::string & str)
{
    std::string ts = timestamp();

    if (config.lockstep)
    {
        write_lockstep("AI: " + ts + " " + str);
    }
    if (config.write_console)
    {
        write_df(out, "AI: " + ts + " " + str, "\n", "\n", [](const std::string & in) -> std::string { return DF2CONSOLE(in); });
    }
    if (config.write_log)
    {
        write_df(logger, ts + " " + str, "\n                 ");
    }
}

void AI::event(const std::string & name, const Json::Value & payload)
{
    if (!eventsJson.is_open())
    {
        return;
    }

    Json::Value wrapper(Json::objectValue);
    wrapper["unix"] = Json::LargestInt(time(nullptr));
    wrapper["year"] = Json::Int(*cur_year);
    wrapper["tick"] = Json::Int(*cur_year_tick);
    wrapper["name"] = name;
    wrapper["payload"] = payload;
    eventsJson << wrapper << std::endl;
}

std::string AI::status()
{
    if (is_embarking())
    {
        return "(embarking)";
    }

    std::ostringstream str;
    str << "Plan: " << plan.status() << "\n";
    str << "Pop: " << pop.status() << "\n";
    str << "Stocks: " << stocks.status() << "\n";
    str << "Camera: " << camera.status() << "\n";
    str << "Event: " << events.status();
    return str.str();
}

template<typename M>
static void report_section(std::ostringstream & out, const std::string & name, M & module, bool html)
{
    if (html)
    {
        out << "<h1 id=\"" << html_escape(name) << "\">" << html_escape(name) << "</h1>";
    }
    else
    {
        out << "# " << name << "\n";
    }
    module.report(out, html);
    if (!html)
    {
        out << "\n";
    }
}

std::string AI::report(bool html)
{
    if (is_embarking())
    {
        if (html)
        {
            return "<i>Report is not available during embark.</i>";
        }
        return "";
    }

    std::ostringstream str;
    report_section(str, "Plan", plan, html);
    report_section(str, "Population", pop, html);
    report_section(str, "Stocks", stocks, html);
    report_section(str, "Events", events, html);
    return str.str();
}

void AI::watch_announcements()
{
    for (auto it = std::find_if(world->status.announcements.rbegin(), world->status.announcements.rend(), [this](df::report *r) -> bool { return r->id == last_announcement_id; }).base(); it != world->status.announcements.end(); it++)
    {
        last_announcement_id = (*it)->id;
        if (!(*it)->flags.bits.announcement ||
            (*it)->type == announcement_type::UNABLE_TO_COMPLETE_BUILDING ||
            (*it)->type == announcement_type::CONSTRUCTION_SUSPENDED)
        {
            continue;
        }

        uint8_t color = uint8_t((*it)->color);
        if ((*it)->bright)
        {
            color |= 64;
        }

        write_lockstep((*it)->text, color);
    }
}
