#include "test_camera_properties_state.h"
#include "camera_properties_state.h"
#include <cstring>
#include <string>
#include <vector>

using namespace revere;
using namespace std;

REGISTER_TEST_FIXTURE(test_camera_properties_state);

void test_camera_properties_state::test_nothing_changed_is_not_dirty()
{
    camera_properties_session s;
    s.begin("cam", false, true, true, "24", "auto", "TCP");
    RTF_ASSERT(!s.dirty(false, true, true, "auto", "TCP", false));
}

void test_camera_properties_state::test_each_setting_makes_it_dirty()
{
    camera_properties_session s;
    s.begin("cam", false, true, true, "24", "auto", "TCP");
    RTF_ASSERT(s.dirty(false, false, true, "auto", "TCP", false));
    RTF_ASSERT(s.dirty(false, true, false, "auto", "TCP", false));
    RTF_ASSERT(s.dirty(false, true, true, "2", "TCP", false));
    RTF_ASSERT(s.dirty(false, true, true, "auto", "UDP", false));
    // The doorbell toggle.
    RTF_ASSERT(s.dirty(true, true, true, "auto", "TCP", false));
    // The streams section changed (a smaller stream chosen).
    RTF_ASSERT(s.dirty(false, true, true, "auto", "TCP", true));
}

void test_camera_properties_state::test_hours_count_only_with_pruning()
{
    camera_properties_session s;
    s.begin("cam", false, true, true, "24", "auto", "TCP");
    strcpy(s.hours_text, "48");
    RTF_ASSERT(s.dirty(false, true, true, "auto", "TCP", false));
    // With pruning off the hours aren't saved, so typing in them is not a change.
    camera_properties_session off;
    off.begin("cam", false, true, false, "24", "auto", "TCP");
    strcpy(off.hours_text, "48");
    RTF_ASSERT(!off.dirty(false, true, false, "auto", "TCP", false));
    // Changing back to what it was is no change.
    strcpy(s.hours_text, "24");
    RTF_ASSERT(!s.dirty(false, true, true, "auto", "TCP", false));
}

void test_camera_properties_state::test_hours_must_be_a_whole_number_of_hours()
{
    camera_properties_session s;
    s.begin("cam", false, true, true, "24", "auto", "TCP");
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
    s.begin("one", false, true, true, "24", "auto", "TCP");
    strcpy(s.hours_text, "99");
    s.tried = true;
    s.begin("two", false, false, false, "12", "1", "UDP");
    RTF_ASSERT(s.camera_id == "two");
    RTF_ASSERT(string(s.hours_text) == "12");
    RTF_ASSERT(!s.tried);
    RTF_ASSERT(!s.dirty(false, false, false, "1", "UDP", false));
}

void test_camera_properties_state::test_the_press_source_counts_only_on_a_doorbell()
{
    camera_properties_session s;
    s.begin("cam", true, true, true, "24", "auto", "TCP", "onvif");
    RTF_ASSERT(!s.dirty(true, true, true, "auto", "TCP", false, "onvif"));
    RTF_ASSERT(s.dirty(true, true, true, "auto", "TCP", false, "reolink_email"));
    // Not a doorbell: the press source isn't saved, so changing it is no change.
    camera_properties_session cam;
    cam.begin("cam", false, true, true, "24", "auto", "TCP", "onvif");
    RTF_ASSERT(!cam.dirty(false, true, true, "auto", "TCP", false, "reolink_email"));
    // Saved as email, back to email: no change.
    camera_properties_session e;
    e.begin("cam", true, true, true, "24", "auto", "TCP", "reolink_email");
    RTF_ASSERT(!e.dirty(true, true, true, "auto", "TCP", false, "reolink_email"));
    RTF_ASSERT(e.dirty(true, true, true, "auto", "TCP", false, "onvif"));
}

void test_camera_properties_state::test_the_press_test_lines()
{
    doorbell_view v;
    v.test_any = true;
    v.test_open = true;
    v.test_sources = {{"onvif", true, 300}, {"reolink_email", false, 0}};
    auto open = press_test_lines(v);
    RTF_ASSERT(open == vector<string>({"ONVIF: yes, at once", "Email: not yet"}));
    v.test_open = false;
    v.test_sources = {{"onvif", false, 0}, {"reolink_email", true, 14400}};
    auto done = press_test_lines(v);
    RTF_ASSERT(done == vector<string>({"ONVIF: no", "Email: yes, 14 s after"}));
    RTF_ASSERT(press_test_lines(doorbell_view()).empty());
}

void test_camera_properties_state::test_the_press_test_verdict()
{
    doorbell_view v;
    RTF_ASSERT(press_test_verdict(v, "onvif").empty());             // no test yet
    v.test_any = true;
    v.test_open = true;
    v.test_sources = {{"onvif", true, 0}};
    RTF_ASSERT(press_test_verdict(v, "onvif").empty());             // still running
    v.test_open = false;
    RTF_ASSERT(press_test_verdict(v, "onvif").find("at once") != string::npos);
    RTF_ASSERT(press_test_verdict(v, "reolink_email").find("choose ONVIF") != string::npos);
    v.test_sources = {{"onvif", false, 0}, {"reolink_email", true, 15000}};
    RTF_ASSERT(press_test_verdict(v, "reolink_email").find("keep Reolink email") != string::npos);
    v.test_sources = {{"onvif", false, 0}};
    RTF_ASSERT(press_test_verdict(v, "onvif").find("choose Reolink email") != string::npos);
    v.test_sources = {{"onvif", false, 0}, {"reolink_email", false, 0}};
    RTF_ASSERT(press_test_verdict(v, "reolink_email").find("Check the email settings") != string::npos);
}

void test_camera_properties_state::test_the_mail_status_lines()
{
    doorbell_view v;
    RTF_ASSERT(mail_status_lines(v, "onvif").empty());
    // Chosen but not saved yet.
    auto unsaved = mail_status_lines(v, "reolink_email");
    RTF_ASSERT(unsaved.size() == 1 && unsaved[0].find("Save") == 0);
    v.saved_source = "reolink_email";
    RTF_ASSERT(mail_status_lines(v, "reolink_email")[0].find("within a few seconds") != string::npos);
    v.last_error = "the port is in use";
    RTF_ASSERT(mail_status_lines(v, "reolink_email")[0].find("the port is in use") != string::npos);
    v.listening = true;
    v.last_error.clear();
    RTF_ASSERT(mail_status_lines(v, "reolink_email")[0].find("No ring by email yet") != string::npos);
    v.last_email_ms = 1764018345000;
    RTF_ASSERT(mail_status_lines(v, "reolink_email")[0] == "Last ring by email: " + clock_text(v.last_email_ms) + ".");
    v.other_mail = 1;
    v.bad_logins = 2;
    auto lines = mail_status_lines(v, "reolink_email");
    RTF_ASSERT(lines.size() == 3);
    RTF_ASSERT(lines[1].find("1 email that wasn't a ring") == 0 && lines[1].find("Ring only") != string::npos);
    RTF_ASSERT(lines[2].find("refused 2 times") != string::npos);
    RTF_ASSERT(clock_text(1764018345000).size() == 8);
}

void test_camera_properties_state::test_a_password_in_groups_of_4()
{
    RTF_ASSERT(in_groups_of_4("Xw7tQ2mLp9RzV4nBc8Hd") == "Xw7t Q2mL p9Rz V4nB c8Hd");
    RTF_ASSERT(in_groups_of_4("abcde") == "abcd e");
    RTF_ASSERT(in_groups_of_4("abcd") == "abcd");
    RTF_ASSERT(in_groups_of_4("").empty());
    RTF_ASSERT(std::string(mail_sender()).find('@') != std::string::npos);
}
