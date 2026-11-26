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
    RTF_ASSERT(open == vector<string>({"ONVIF: arrived right away", "Email: not yet"}));
    v.test_open = false;
    v.test_sources = {{"onvif", false, 0}, {"reolink_email", true, 14400}};
    auto done = press_test_lines(v);
    RTF_ASSERT(done == vector<string>({"ONVIF: didn't arrive", "Email: arrived after 14 seconds"}));
    v.test_sources = {{"reolink_email", true, 1200}};
    RTF_ASSERT(press_test_lines(v) == vector<string>({"Email: arrived after 1 second"}));
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
    RTF_ASSERT(press_test_verdict(v, "onvif") == "The doorbell sent the press over ONVIF. Rings will arrive right away.");
    RTF_ASSERT(press_test_verdict(v, "reolink_email") == "The doorbell sent the press over ONVIF. Choose ONVIF so rings arrive right away.");
    v.test_sources = {{"onvif", false, 0}, {"reolink_email", true, 15000}};
    RTF_ASSERT(press_test_verdict(v, "reolink_email") == "The doorbell only emailed the press. Keep Reolink email.");
    v.test_sources = {{"onvif", false, 0}};
    RTF_ASSERT(press_test_verdict(v, "onvif") == "No press arrived. If this is a Reolink doorbell, choose Reolink email and set it up.");
    v.test_sources = {{"onvif", false, 0}, {"reolink_email", false, 0}};
    RTF_ASSERT(press_test_verdict(v, "reolink_email") == "No press arrived. Check the email settings in the Reolink app, then test again.");
}

void test_camera_properties_state::test_the_mail_status_lines()
{
    doorbell_view v;
    RTF_ASSERT(mail_status_lines(v, "onvif").empty());
    // Chosen but not saved yet.
    auto unsaved = mail_status_lines(v, "reolink_email");
    RTF_ASSERT(unsaved.size() == 1 && unsaved[0] == "Save to start receiving this doorbell's email.");
    v.saved_source = "reolink_email";
    RTF_ASSERT(mail_status_lines(v, "reolink_email")[0] == "Revere is getting ready to receive the doorbell's email. This takes a few seconds.");
    v.last_error = "the port is in use";
    RTF_ASSERT(mail_status_lines(v, "reolink_email")[0] == "Revere can't receive the doorbell's email: the port is in use");
    v.listening = true;
    v.last_error.clear();
    RTF_ASSERT(mail_status_lines(v, "reolink_email")[0] == "Ready. The doorbell hasn't emailed a press yet. Press it once to check.");
    v.last_email_ms = 1764018345000;
    RTF_ASSERT(mail_status_lines(v, "reolink_email")[0] == "The doorbell last emailed a press at " + clock_text(v.last_email_ms) + ".");
    v.other_mail = 1;
    v.bad_logins = 2;
    auto lines = mail_status_lines(v, "reolink_email");
    RTF_ASSERT(lines.size() == 3);
    RTF_ASSERT(lines[1] == "The doorbell sent 1 email that wasn't a press. In the Reolink app, set the email Alarm Schedule to Ring only.");
    RTF_ASSERT(lines[2] == "The doorbell's email login failed 2 times. Check the user name and password in the Reolink app.");
    v.other_mail = 3;
    v.bad_logins = 1;
    lines = mail_status_lines(v, "reolink_email");
    RTF_ASSERT(lines[1].find("sent 3 emails that weren't presses.") != string::npos);
    RTF_ASSERT(lines[2].find("failed 1 time.") != string::npos);
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
