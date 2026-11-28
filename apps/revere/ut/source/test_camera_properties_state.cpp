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

void test_camera_properties_state::test_the_press_source_counts_only_on_a_doorbell()
{
    camera_properties_session s;
    s.begin("cam", true, true, true, "24", "auto", "TCP", "main", "onvif");
    RTF_ASSERT(!s.dirty(true, true, true, "auto", "TCP", "main", false, "onvif"));
    RTF_ASSERT(s.dirty(true, true, true, "auto", "TCP", "main", false, "reolink_email"));
    // Not a doorbell: the press source isn't saved, so changing it is no change.
    camera_properties_session cam;
    cam.begin("cam", false, true, true, "24", "auto", "TCP", "main", "onvif");
    RTF_ASSERT(!cam.dirty(false, true, true, "auto", "TCP", "main", false, "reolink_email"));
    // Saved as email, back to email: no change.
    camera_properties_session e;
    e.begin("cam", true, true, true, "24", "auto", "TCP", "main", "reolink_email");
    RTF_ASSERT(!e.dirty(true, true, true, "auto", "TCP", "main", false, "reolink_email"));
    RTF_ASSERT(e.dirty(true, true, true, "auto", "TCP", "main", false, "onvif"));
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
    // An email that wasn't a press came during the test (the doorbell's Visitor alert is off, or the app's Test email).
    v.test_sources = {{"onvif", false, 0}, {"reolink_email", false, 0}};
    v.test_other_mail = true;
    RTF_ASSERT(press_test_lines(v) == vector<string>({"ONVIF: didn't arrive", "Email: didn't arrive", "Got an email from the doorbell, but it wasn't a press."}));
    // The doorbell's clock 3 s or more off: said after a test, with what to do; never before one, nor while unknown.
    v.test_other_mail = false;
    v.clock_known = true;
    v.clock_offset_ms = -9000;
    RTF_ASSERT(press_test_lines(v).back() == "The doorbell's clock is 9 seconds slow. Turn on time sync (NTP) in the Reolink app.");
    v.clock_offset_ms = 3000;
    RTF_ASSERT(press_test_lines(v).back() == "The doorbell's clock is 3 seconds fast. Turn on time sync (NTP) in the Reolink app.");
    v.clock_offset_ms = -2999;
    RTF_ASSERT(press_test_lines(v).size() == 2);
    v.clock_offset_ms = 2999;
    RTF_ASSERT(press_test_lines(v).size() == 2);
    v.clock_offset_ms = -3000;
    RTF_ASSERT(press_test_lines(v).size() == 3);
    v.clock_known = false;
    RTF_ASSERT(press_test_lines(v).size() == 2);
    doorbell_view untested;
    untested.clock_known = true;
    untested.clock_offset_ms = -9000;
    RTF_ASSERT(press_test_lines(untested).empty());
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
    RTF_ASSERT(lines[2] == "The doorbell's email login failed 2 times. Check the password in the Reolink app.");
    v.other_mail = 3;
    v.bad_logins = 1;
    lines = mail_status_lines(v, "reolink_email");
    RTF_ASSERT(lines[1].find("sent 3 emails that weren't presses.") != string::npos);
    RTF_ASSERT(lines[2].find("failed 1 time.") != string::npos);
    // Locked for its wrong passwords: whole minutes left, rounded up.
    RTF_ASSERT(lines.size() == 3);
    v.locked_for_ms = 7 * 60000 - 1000;
    lines = mail_status_lines(v, "reolink_email");
    RTF_ASSERT(lines.size() == 4 && lines[3] == "Too many wrong passwords from the doorbell. Locked for 7 more minutes.");
    v.locked_for_ms = 1;
    RTF_ASSERT(mail_status_lines(v, "reolink_email")[3] == "Too many wrong passwords from the doorbell. Locked for 1 more minute.");
    v.locked_for_ms = 60001;
    RTF_ASSERT(mail_status_lines(v, "reolink_email")[3] == "Too many wrong passwords from the doorbell. Locked for 2 more minutes.");
    RTF_ASSERT(clock_text(1764018345000).size() == 8);
}

void test_camera_properties_state::test_the_password_is_shown_as_it_is()
{
    // All 8 characters together, no space or dash, as Copy copies it; hidden until shown.
    RTF_ASSERT(password_shown("k7mq2xpa", true) == "k7mq2xpa");
    RTF_ASSERT(password_shown("k7mq2xpa", false) == std::string(12, '*'));
    RTF_ASSERT(password_shown("", true).empty());
}

void test_camera_properties_state::test_the_mail_setup_rows()
{
    // What the Reolink app asks for, in its order and with its names: no user name.
    doorbell_view v;
    v.server = "192.168.1.20";
    v.port = 2525;
    v.sender = "doorbell@revere.video";
    v.password = "k7mq2xpa";
    v.recipient = "pressed@revere.video";
    auto rows = mail_setup_rows(v);
    vector<string> names;
    for(auto& r : rows)
        names.push_back(r.name);
    RTF_ASSERT(names == vector<string>({"SMTP server", "SMTP port", "SSL or TLS", "Sender's email", "Password", "Recipient"}));
    RTF_ASSERT(rows[0].value == "192.168.1.20" && rows[1].value == "2525" && rows[2].value == "On");
    RTF_ASSERT(rows[3].value == "doorbell@revere.video" && rows[5].value == "pressed@revere.video");
    // Only the password is secret, and every row but SSL or TLS can be copied.
    for(size_t i = 0; i < rows.size(); ++i)
    {
        RTF_ASSERT(rows[i].secret == (i == 4));
        RTF_ASSERT(rows[i].copy == (i != 2));
    }
    RTF_ASSERT(rows[4].value == "k7mq2xpa");
}

void test_camera_properties_state::test_the_column_width()
{
    // About 36 characters of the pane's text: 792 px at Windows' 22 px, 576 at macOS's 16.
    RTF_ASSERT(properties_content_width(1400.0f, 22.0f) == 792.0f);
    RTF_ASSERT(properties_content_width(1400.0f, 16.0f) == 576.0f);
    // Never under 420 px while there's room for it, and never more than the room there is.
    RTF_ASSERT(properties_content_width(1400.0f, 10.0f) == 420.0f);
    RTF_ASSERT(properties_content_width(600.0f, 22.0f) == 600.0f);
    RTF_ASSERT(properties_content_width(300.0f, 10.0f) == 300.0f);
}

void test_camera_properties_state::test_a_button_beside_its_text()
{
    // Beside it while the text keeps its least width after the button and a 16 px gap; under it otherwise.
    RTF_ASSERT(fits_beside(600.0f, 150.0f, 200.0f));
    RTF_ASSERT(fits_beside(366.0f, 150.0f, 200.0f));
    RTF_ASSERT(!fits_beside(365.0f, 150.0f, 200.0f));
}

void test_camera_properties_state::test_the_label_column()
{
    // The widest label and a 20 px gap, while the values keep their least width.
    RTF_ASSERT(label_column_width(120.0f, 600.0f, 200.0f) == 140.0f);
    RTF_ASSERT(label_column_width(120.0f, 340.0f, 200.0f) == 140.0f);
    // Else each label goes above its value.
    RTF_ASSERT(label_column_width(120.0f, 339.0f, 200.0f) == 0.0f);
}

namespace
{
properties_nav drawn(const std::vector<std::pair<std::string, float>>& sections)
{
    properties_nav n;
    n.begin_frame();
    for(auto& s : sections)
        n.add(s.first, s.second);
    n.end_frame();
    return n;
}
}

void test_camera_properties_state::test_the_section_tabs_are_the_sections_drawn()
{
    properties_nav n;
    RTF_ASSERT(n.titles.empty());
    RTF_ASSERT(n.active() == -1);
    n = drawn({{"Recording", 4.0f}, {"Detection", 300.0f}, {"Doorbell", 700.0f}});
    RTF_ASSERT((n.titles == std::vector<std::string>{"Recording", "Detection", "Doorbell"}));
    RTF_ASSERT((n.tops == std::vector<float>{4.0f, 300.0f, 700.0f}));
    // A frame still being drawn changes nothing yet; a camera with fewer sections has fewer tabs.
    n.begin_frame();
    n.add("Recording", 4.0f);
    RTF_ASSERT(n.titles.size() == 3);
    n.end_frame();
    RTF_ASSERT((n.titles == std::vector<std::string>{"Recording"}));
    // Opening another camera starts over.
    n.jump = 0;
    n.scroll_y = 50.0f;
    camera_properties_session s;
    s.nav = n;
    s.begin("cam", false, true, true, "24", "auto", "TCP", "main");
    RTF_ASSERT(s.nav.titles.empty() && s.nav.jump == -1 && s.nav.scroll_y == 0.0f);
}

void test_camera_properties_state::test_the_section_in_view()
{
    auto n = drawn({{"Recording", 4.0f}, {"Detection", 300.0f}, {"Streams", 700.0f}, {"Doorbell", 900.0f}});
    n.scroll_max = 2000.0f;
    n.scroll_y = 0.0f;
    RTF_ASSERT(n.active() == 0);
    // A section is in view once it is within a line or so of the top.
    n.scroll_y = 270.0f;
    RTF_ASSERT(n.active() == 0);
    n.scroll_y = 280.0f;
    RTF_ASSERT(n.active() == 1);
    n.scroll_y = 880.0f;
    RTF_ASSERT(n.active() == 3);
    // Scrolled to the bottom, the last section is the one in view though it never reached the top.
    n.scroll_max = 600.0f;
    n.scroll_y = 600.0f;
    RTF_ASSERT(n.active() == 3);
    n.scroll_y = 500.0f;
    RTF_ASSERT(n.active() == 1);
    // Content that doesn't scroll is at its top.
    n.scroll_max = 0.0f;
    n.scroll_y = 0.0f;
    RTF_ASSERT(n.active() == 0);
}

void test_camera_properties_state::test_a_tab_scrolls_to_its_section()
{
    auto n = drawn({{"Recording", 4.0f}, {"Detection", 300.0f}, {"Doorbell", 900.0f}});
    n.scroll_max = 500.0f;
    RTF_ASSERT(n.scroll_target(0) == 4.0f);
    RTF_ASSERT(n.scroll_target(1) == 300.0f);
    // Not past what the content scrolls.
    RTF_ASSERT(n.scroll_target(2) == 500.0f);
    // Before the content has been measured, the section's own top.
    n.scroll_max = 0.0f;
    RTF_ASSERT(n.scroll_target(2) == 900.0f);
    // No such section: stay where it is.
    n.scroll_y = 120.0f;
    RTF_ASSERT(n.scroll_target(7) == 120.0f);
    RTF_ASSERT(n.scroll_target(-1) == 120.0f);
    // A jump to a tab the next frame no longer draws is dropped.
    n.jump = 2;
    n.begin_frame();
    n.add("Recording", 4.0f);
    n.end_frame();
    RTF_ASSERT(n.jump == -1);
}


void test_camera_properties_state::test_a_clicked_tab_stays_marked_until_the_user_scrolls()
{
    // The last section can't reach the top (the content scrolls to 500 only), so it would else lose the mark.
    auto n = drawn({{"Recording", 4.0f}, {"Detection", 300.0f}, {"Streams", 450.0f}, {"Doorbell", 900.0f}});
    n.observe(0.0f, 500.0f);
    RTF_ASSERT(n.active() == 0);
    // Streams is clicked: it is marked at once, though the scroll that follows (to 450) leaves Detection nearer the top
    // for a content that stops at 400.
    n.observe(0.0f, 400.0f);
    n.pin(2);
    RTF_ASSERT(n.jump == 2 && n.active() == 2);
    // The scroll settles over a few frames, and the mark stays.
    for(float y : {0.0f, 150.0f, 400.0f, 400.0f, 400.0f, 400.0f})
    {
        n.observe(y, 400.0f);
        RTF_ASSERT(n.active() == 2);
    }
    // The user scrolls: back to what is in view.
    n.observe(200.0f, 400.0f);
    RTF_ASSERT(n.pinned == -1);
    RTF_ASSERT(n.active() == 0);
    // A new frame set without that tab drops the pin; opening another camera starts over.
    n.pin(3);
    n.begin_frame();
    n.add("Recording", 4.0f);
    n.end_frame();
    RTF_ASSERT(n.pinned == -1);
    n.pin(0);
    n.reset();
    RTF_ASSERT(n.pinned == -1 && n.jump == -1);
}
