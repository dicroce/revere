#include "test_camera_properties_state.h"
#include "camera_properties_state.h"
#include <cstring>

using namespace revere;
using namespace std;

REGISTER_TEST_FIXTURE(test_camera_properties_state);

void test_camera_properties_state::test_nothing_changed_is_not_dirty()
{
    camera_properties_session s;
    s.begin("cam", false, true, true, "24", "auto", "TCP", "main");
    RTF_ASSERT(!s.dirty(false, true, true, "auto", "TCP", "main", false));
}

void test_camera_properties_state::test_each_setting_makes_it_dirty()
{
    camera_properties_session s;
    s.begin("cam", false, true, true, "24", "auto", "TCP", "main");
    RTF_ASSERT(s.dirty(false, false, true, "auto", "TCP", "main", false));
    RTF_ASSERT(s.dirty(false, true, false, "auto", "TCP", "main", false));
    RTF_ASSERT(s.dirty(false, true, true, "2", "TCP", "main", false));
    RTF_ASSERT(s.dirty(false, true, true, "auto", "UDP", "main", false));
    // The doorbell toggle.
    RTF_ASSERT(s.dirty(true, true, true, "auto", "TCP", "main", false));
    // The streams section changed (a smaller stream chosen).
    RTF_ASSERT(s.dirty(false, true, true, "auto", "TCP", "main", true));
}

void test_camera_properties_state::test_the_stream_recorded_is_a_setting()
{
    camera_properties_session s;
    s.begin("cam", false, true, true, "24", "auto", "TCP", "main");
    RTF_ASSERT(!s.dirty(false, true, true, "auto", "TCP", "main", false));
    RTF_ASSERT(s.dirty(false, true, true, "auto", "TCP", "sub", false));
    // Changing it back is no change; one that started on the smaller stream is the other way round.
    camera_properties_session t;
    t.begin("cam", false, true, true, "24", "auto", "TCP", "sub");
    RTF_ASSERT(!t.dirty(false, true, true, "auto", "TCP", "sub", false));
    RTF_ASSERT(t.dirty(false, true, true, "auto", "TCP", "main", false));
}

void test_camera_properties_state::test_what_record_from_says()
{
    RTF_ASSERT(record_from_description("main").find("main stream") != string::npos);
    RTF_ASSERT(record_from_description("sub").find("never opens its main stream") != string::npos);
    // Nothing to add when it is as set; a line when it is not.
    RTF_ASSERT(record_from_status("main", true).empty() && record_from_status("sub", true).empty());
    RTF_ASSERT(record_from_status("sub", false).find("records from the main stream") != string::npos);
    RTF_ASSERT(record_from_status("main", false).find("has to be set up and working first") != string::npos);
}

void test_camera_properties_state::test_hours_count_only_with_pruning()
{
    camera_properties_session s;
    s.begin("cam", false, true, true, "24", "auto", "TCP", "main");
    strcpy(s.hours_text, "48");
    RTF_ASSERT(s.dirty(false, true, true, "auto", "TCP", "main", false));
    // With pruning off the hours aren't saved, so typing in them is not a change.
    camera_properties_session off;
    off.begin("cam", false, true, false, "24", "auto", "TCP", "main");
    strcpy(off.hours_text, "48");
    RTF_ASSERT(!off.dirty(false, true, false, "auto", "TCP", "main", false));
    // Changing back to what it was is no change.
    strcpy(s.hours_text, "24");
    RTF_ASSERT(!s.dirty(false, true, true, "auto", "TCP", "main", false));
}

void test_camera_properties_state::test_hours_must_be_a_whole_number_of_hours()
{
    camera_properties_session s;
    s.begin("cam", false, true, true, "24", "auto", "TCP", "main");
    RTF_ASSERT(s.hours_ok());
    for(auto bad : {"", "0", "1.5", "-3", "abc", "1234567"})
    {
        strcpy(s.hours_text, bad);
        RTF_ASSERT(!s.hours_ok());
    }
    strcpy(s.hours_text, "1");
    RTF_ASSERT(s.hours_ok());
}

void test_camera_properties_state::test_begin_starts_over()
{
    camera_properties_session s;
    s.begin("one", false, true, true, "24", "auto", "TCP", "main");
    strcpy(s.hours_text, "99");
    s.tried = true;
    s.begin("two", false, false, false, "12", "1", "UDP", "main");
    RTF_ASSERT(s.camera_id == "two");
    RTF_ASSERT(string(s.hours_text) == "12");
    RTF_ASSERT(!s.tried);
    RTF_ASSERT(!s.dirty(false, false, false, "1", "UDP", "main", false));
}
