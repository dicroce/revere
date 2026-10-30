#include "test_view_status.h"
#include "r_ui_utils/view_status.h"
#include <string>

using namespace r_ui_utils;
using namespace std;
using namespace std::chrono;

REGISTER_TEST_FIXTURE(test_view_status);

static const steady_clock::time_point T0 = steady_clock::time_point() + hours(1);

static view_signals playing(steady_clock::time_point last_present)
{
    view_signals s;
    s.got_sample = true;
    s.presented = true;
    s.last_present = last_present;
    return s;
}

void test_view_status::test_start_steps()
{
    // Live has started and nothing has come from the camera yet.
    view_signals s;
    RTF_ASSERT(view_stage_now(s, T0) == view_stage::connecting);

    // Video arrives, but no picture has decoded (waiting for a keyframe).
    s.got_sample = true;
    RTF_ASSERT(view_stage_now(s, T0) == view_stage::waiting_for_picture);
}

void test_view_status::test_playing_says_nothing()
{
    RTF_ASSERT(view_stage_now(playing(T0), T0) == view_stage::none);
    RTF_ASSERT(view_stage_now(playing(T0), T0 + milliseconds(40)) == view_stage::none);
}

void test_view_status::test_routine_gap_unlabelled()
{
    // A keyframe gap of about 2 s holds the last picture with no label.
    RTF_ASSERT(view_stage_now(playing(T0), T0 + milliseconds(2000)) == view_stage::none);
    RTF_ASSERT(view_stage_now(playing(T0), T0 + VIEW_STALL_AFTER - milliseconds(1)) == view_stage::none);
}

void test_view_status::test_long_freeze()
{
    RTF_ASSERT(view_stage_now(playing(T0), T0 + VIEW_STALL_AFTER) == view_stage::waiting_for_video);
    RTF_ASSERT(view_stage_now(playing(T0), T0 + seconds(9)) == view_stage::waiting_for_video);
}

void test_view_status::test_restart()
{
    // A dead stream being restarted says so until live shows again,
    // whatever the new pipeline has got to.
    view_signals s;
    s.restarting = true;
    RTF_ASSERT(view_stage_now(s, T0) == view_stage::reconnecting);
    s.got_sample = true;
    RTF_ASSERT(view_stage_now(s, T0) == view_stage::reconnecting);
    auto p = playing(T0 - seconds(20));
    p.restarting = true;
    RTF_ASSERT(view_stage_now(p, T0) == view_stage::reconnecting);
}

void test_view_status::test_not_live()
{
    // Playback, a paused timeline or a still picked from it: no live steps.
    view_signals s;
    s.live = false;
    RTF_ASSERT(view_stage_now(s, T0) == view_stage::none);
    s.restarting = true;
    RTF_ASSERT(view_stage_now(s, T0) == view_stage::none);
    auto p = playing(T0 - seconds(30));
    p.live = false;
    RTF_ASSERT(view_stage_now(p, T0) == view_stage::none);
}

void test_view_status::test_label_holds()
{
    view_status vs;

    // A label shows at once...
    view_signals s;
    RTF_ASSERT(vs.update(s, T0) == view_stage::connecting);

    // ...and the first picture 200 ms later doesn't take it straight off.
    auto p = playing(T0 + milliseconds(200));
    RTF_ASSERT(vs.update(p, T0 + milliseconds(200)) == view_stage::connecting);
    RTF_ASSERT(vs.update(p, T0 + VIEW_LABEL_MIN_SHOW - milliseconds(1)) == view_stage::connecting);
    RTF_ASSERT(vs.update(p, T0 + VIEW_LABEL_MIN_SHOW) == view_stage::none);

    // Nothing to hold back when no label is showing: the freeze label comes
    // as soon as the freeze is long enough.
    auto frozen_at = T0 + VIEW_LABEL_MIN_SHOW + milliseconds(10);
    auto f = playing(frozen_at);
    RTF_ASSERT(vs.update(f, frozen_at + VIEW_STALL_AFTER) == view_stage::waiting_for_video);

    // A picture right after it: the label stays its full second, then goes.
    auto back = frozen_at + VIEW_STALL_AFTER + milliseconds(100);
    RTF_ASSERT(vs.update(playing(back), back) == view_stage::waiting_for_video);
    RTF_ASSERT(vs.update(playing(back), frozen_at + VIEW_STALL_AFTER + VIEW_LABEL_MIN_SHOW) == view_stage::none);
    RTF_ASSERT(vs.shown() == view_stage::none);
}

void test_view_status::test_texts()
{
    RTF_ASSERT(string(view_stage_text(view_stage::none)).empty());
    RTF_ASSERT(string(view_stage_text(view_stage::connecting)) == "Connecting to camera...");
    RTF_ASSERT(string(view_stage_text(view_stage::waiting_for_picture)) == "Waiting for the first picture...");
    RTF_ASSERT(string(view_stage_text(view_stage::waiting_for_video)) == "Waiting for video...");
    RTF_ASSERT(string(view_stage_text(view_stage::reconnecting)) == "Reconnecting...");
}
