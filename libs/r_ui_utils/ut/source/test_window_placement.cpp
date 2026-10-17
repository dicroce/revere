
#include "test_window_placement.h"
#include "r_ui_utils/window_placement.h"

using namespace r_ui_utils;
using namespace std;

REGISTER_TEST_FIXTURE(test_window_placement);

// A 1920x1080 laptop screen with a 25-point menu bar, as SDL_GetDisplayUsableBounds reports it.
static const window_rect MAIN {0, 25, 1920, 1055};

void test_window_placement::test_kept_where_it_was()
{
    auto r = restore_window_rect({200, 150, 1280, 720}, {MAIN});
    RTF_ASSERT(r && *r == (window_rect{200, 150, 1280, 720}));
}

void test_window_placement::test_display_gone()
{
    // Saved on a monitor to the right that's been unplugged.
    RTF_ASSERT(!restore_window_rect({2200, 100, 1280, 720}, {MAIN}));
}

void test_window_placement::test_partly_off_is_pulled_in()
{
    // Hanging off the right and bottom edges: moved fully inside, same size.
    auto r = restore_window_rect({1500, 700, 1280, 720}, {MAIN});
    RTF_ASSERT(r && *r == (window_rect{640, 360, 1280, 720}));
}

void test_window_placement::test_too_big_shrinks()
{
    // Saved on a bigger monitor, now on the laptop alone: shrunk to the usable area.
    auto r = restore_window_rect({100, 100, 2400, 1400}, {MAIN});
    RTF_ASSERT(r && *r == (window_rect{0, 25, 1920, 1055}));
}

void test_window_placement::test_second_display_left_of_main()
{
    // A second monitor left of the main one has negative x; a window on it stays there.
    window_rect left {-2560, -200, 2560, 1440};
    auto r = restore_window_rect({-2000, 0, 1280, 720}, {MAIN, left});
    RTF_ASSERT(r && *r == (window_rect{-2000, 0, 1280, 720}));

    // Straddling both, it goes to the one holding more of it.
    auto s = restore_window_rect({-300, 100, 1000, 600}, {MAIN, left});
    RTF_ASSERT(s && *s == (window_rect{0, 100, 1000, 600}));
}

void test_window_placement::test_barely_visible_is_not_kept()
{
    // Only a 40-point sliver on screen: too little to grab, so it's centered instead.
    RTF_ASSERT(!restore_window_rect({1880, 200, 1280, 720}, {MAIN}));
    // Exactly the minimum is enough.
    RTF_ASSERT(restore_window_rect({1920 - MIN_VISIBLE_W, 200, 1280, 720}, {MAIN}).has_value());
}

void test_window_placement::test_title_bar_kept_on_screen()
{
    // At the top of the usable area, the frame's title bar would sit above the screen: moved down for it.
    auto r = restore_window_rect({200, 25, 1280, 720}, {MAIN}, 30);
    RTF_ASSERT(r && *r == (window_rect{200, 55, 1280, 720}));
    // A window as tall as the screen gets shorter rather than losing its title bar.
    auto t = restore_window_rect({0, 25, 1920, 1055}, {MAIN}, 30);
    RTF_ASSERT(t && *t == (window_rect{0, 55, 1920, 1025}));
}

void test_window_placement::test_nothing_saved()
{
    RTF_ASSERT(!restore_window_rect({0, 0, 0, 0}, {MAIN}));
    RTF_ASSERT(!restore_window_rect({200, 150, 1280, 720}, {}));
}
