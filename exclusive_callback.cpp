#include "ai.h"
#include "exclusive_callback.h"
#include "debug.h"

#include "modules/Gui.h"
#include "modules/Screen.h"

#include "df/viewscreen.h"

#undef Key
#undef Char

ExclusiveCallback::ExclusiveCallback(const std::string & description, size_t wait_multiplier) :
    out_proxy(),
    pull(nullptr),
    push([&](coroutine_t::pull_type & input) { init(input); }),
    wait_multiplier(wait_multiplier),
    wait_frames(0),
    did_delay(true),
    feed_keys(),
    expectedScreen(),
    expectedFocus(),
    expectedParentFocus(),
    description(description),
    dfplex_blacklist(false)
{
    if (wait_multiplier < 1)
    {
        wait_multiplier = 1;
    }
}

ExclusiveCallback::~ExclusiveCallback()
{
}

void ExclusiveCallback::KeyNoDelay(df::interface_key key)
{
    feed_keys.push_back(key);
}

void ExclusiveCallback::Key(df::interface_key key, const char *filename, int lineno)
{
    checkScreen(filename, lineno);
    KeyNoDelay(key);
    Delay();
}

const static char safe_char[128] =
{
    'C', 'u', 'e', 'a', 'a', 'a', 'a', 'c', 'e', 'e', 'e', 'i', 'i', 'i', 'A', 'A',
    'E', 0, 0, 'o', 'o', 'o', 'u', 'u', 'y', 'O', 'U', 0, 0, 0, 0, 0,
    'a', 'i', 'o', 'u', 'n', 'N', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

void ExclusiveCallback::Char(char c, const char *filename, int lineno)
{
    if (c < 0)
    {
        c = safe_char[(uint8_t)c - 128];
    }
    Key(Screen::charToKey(c), filename, lineno);
}

void ExclusiveCallback::Delay(size_t frames)
{
    for (size_t i = 0; i < frames; i++)
    {
        out_proxy.clear();
        out_proxy.set(*(*pull)().get());
    }

    // Wait until we have an actual viewscreen.
    while (Screen::isDismissed(Gui::getCurViewscreen(false)))
    {
        size_t real_wait_multiplier = wait_multiplier;
        wait_multiplier = 1;
        out_proxy.clear();
        out_proxy.set(*(*pull)().get());
        wait_multiplier = real_wait_multiplier;
    }

    did_delay = true;
}

void ExclusiveCallback::AssertDelayed()
{
    DFAI_ASSERT(did_delay, "previous iteration of exclusive callback \"" << description << "\" did not call Delay.");
    did_delay = false;
}

void ExclusiveCallback::checkScreen(const char *filename, int lineno)
{
    if (!expectedScreen)
    {
        return;
    }

    bool first = true;
    for (;;)
    {
        df::viewscreen *curview = Gui::getCurViewscreen(true);


        auto strings = Gui::getFocusStrings(curview);
        auto stringsParent = Gui::getFocusStrings(curview->parent);

        bool isExpectedScreen = expectedScreen->is_instance(curview) &&
            (expectedFocus.empty() || std::find(strings.begin(), strings.end(), expectedFocus) != strings.end()) &&
            (expectedParentFocus.empty() || std::find(strings.begin(), strings.end(), expectedParentFocus) != strings.end());

        if (first)
        {
            //DFAI_ASSERT_LOC(isExpectedScreen,
            //    "expected screen to be " << expectedScreen->getName() << ":" << expectedFocus << ":" << expectedParentFocus <<
            //    ", but it is " << virtual_identity::get(curview)->getName() << ":" << Gui::getFocusStrings(curview) << ":" << Gui::getFocusStrings(curview->parent),
            //    filename, lineno);

            DFAI_ASSERT_LOC(isExpectedScreen,
                "expected screen to be " << expectedScreen->getName() << ":" << expectedFocus << ":" << expectedParentFocus <<
                ", but it is " << virtual_identity::get(curview)->getName() << ":" << " TODO " << ":" << " TODO ",
                filename, lineno);
        }

        if (isExpectedScreen)
        {
            return;
        }

        first = false;

        Delay();
    }
}

bool ExclusiveCallback::run(color_ostream & out, const std::function<void(std::vector<df::interface_key> &)> & send_keys)
{
    if (wait_frames)
    {
        wait_frames--;
        return false;
    }

    bool done = !push(&out);
    if (!feed_keys.empty())
    {
        send_keys(feed_keys);
    }

    if (!done)
    {
        wait_frames = wait_multiplier - 1;
        return false;
    }

    return true;
}

void ExclusiveCallback::init(coroutine_t::pull_type & input)
{
    pull = &input;
    out_proxy.set(*pull->get());
    Run(out_proxy);
    out_proxy.clear();

    // Make sure we wait for the screen to go back to normal if our last calls before returning were to KeyNoWait.
    if (!feed_keys.empty())
    {
        wait_multiplier = 1;
        Delay();
    }
}
