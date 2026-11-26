#ifndef __revere_camera_properties_state_h
#define __revere_camera_properties_state_h

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace revere
{

// What a camera's properties looked like when the pane opened, so the pane knows whether anything changed (Save is
// off until it has) and whether leaving would lose something.
struct camera_properties_session
{
    std::string camera_id;
    bool doorbell {false};
    bool motion {false};
    bool prune {false};
    std::string hours;
    std::string tier;
    std::string transport;
    // A doorbell's press source: "onvif" or "reolink_email".
    std::string press_source {"onvif"};
    // The hours field's text.
    char hours_text[16] {0};
    // Save was tried: show what's wrong with the hours.
    bool tried {false};

    void begin(const std::string& id, bool doorbell_on, bool motion_on, bool prune_on, const std::string& hours_now, const std::string& tier_now,
               const std::string& transport_now, const std::string& press_source_now = "onvif")
    {
        press_source = press_source_now;
        camera_id = id;
        doorbell = doorbell_on;
        motion = motion_on;
        prune = prune_on;
        hours = hours_now;
        tier = tier_now;
        transport = transport_now;
        std::snprintf(hours_text, sizeof(hours_text), "%s", hours_now.c_str());
        tried = false;
    }

    // The hours field holds a whole number of hours, 1 or more.
    bool hours_ok() const
    {
        const std::string h(hours_text);
        return !h.empty() && h.size() <= 6 && std::all_of(h.begin(), h.end(), [](unsigned char c) { return std::isdigit(c); }) && std::stol(h) >= 1;
    }

    // Anything differs from when the pane opened. The hours only count while pruning is on, and the press source while
    // it's a doorbell (they aren't saved otherwise).
    bool dirty(bool doorbell_now, bool motion_now, bool prune_now, const std::string& tier_now, const std::string& transport_now, bool streams_changed,
               const std::string& press_source_now = "onvif") const
    {
        if(streams_changed || doorbell_now != doorbell || motion_now != motion || prune_now != prune || tier_now != tier || transport_now != transport)
            return true;
        if(doorbell_now && press_source_now != press_source)
            return true;
        return prune_now && std::string(hours_text) != hours;
    }
};

// A doorbell's press setup as the Doorbell section shows it: filled by the app about once a second while the pane is
// open (r_stream_keeper::doorbell_presses and doorbell_mail_setup). Reading it changes nothing on the camera, so it's
// never an unsaved change.
struct doorbell_view
{
    bool loaded {false};
    std::string saved_source {"onvif"};
    // A doorbell set to email whose presses now come over ONVIF too.
    bool suggest_onvif {false};
    int64_t last_email_ms {0};   // 0: none since revere started

    // "Press the doorbell now": its window, and each source's report.
    struct source_result
    {
        std::string source;
        bool seen {false};
        int64_t after_ms {0};    // how long after the press its report came
    };
    bool test_any {false};
    bool test_open {false};
    int64_t test_ends_ms {0};
    std::vector<source_result> test_sources;

    // What to type in the Reolink app; has_mail false while the box has no address toward the doorbell. The password is
    // read once (has_login), and again when a new one is made; the rest is read with the status.
    bool has_mail {false};
    bool has_login {false};
    std::string server;
    int port {0};
    std::string sender;
    std::string password;
    std::string recipient;
    bool listening {false};
    uint64_t other_mail {0};
    uint64_t bad_logins {0};
    std::string last_error;

    // The pane's own: the password shown in plain text, and "New password" asking before it makes one.
    bool reveal {false};
    bool confirm_new_password {false};
};

inline std::string press_source_name(const std::string& source) { return source == "reolink_email" ? "Email" : "ONVIF"; }

// The rows to type into the Reolink app (Settings > Email Alerts > Email Settings), named and ordered as the app has
// them: it has no user field (it logs in with the sender's email, and the listener takes any user with the password).
struct mail_setup_row
{
    const char* key;
    const char* name;
    std::string value;
    bool copy;      // a Copy button
    bool secret;    // the password: hidden until shown, in groups of 4
};
inline std::vector<mail_setup_row> mail_setup_rows(const doorbell_view& v)
{
    return {
        {"server", "SMTP server", v.server, true, false},
        {"port", "SMTP port", std::to_string(v.port), true, false},
        {"tls", "SSL or TLS", "On", false, false},
        {"sender", "Sender's email", v.sender, true, false},
        {"password", "Password", v.password, true, true},
        {"recipient", "Recipient", v.recipient, true, false},
    };
}

// A password shown for typing, in groups of 4: "Xw7t Q2mL p9Rz". Copy copies it as it is.
inline std::string in_groups_of_4(const std::string& s)
{
    std::string out;
    for(size_t i = 0; i < s.size(); ++i)
    {
        if(i && i % 4 == 0)
            out += ' ';
        out += s[i];
    }
    return out;
}

// A time today as the box shows it, "13:16:35".
inline std::string clock_text(int64_t ms)
{
    time_t secs = (time_t)(ms / 1000);
    tm t {};
#ifdef IS_WINDOWS
    localtime_s(&t, &secs);
#else
    localtime_r(&secs, &t);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &t);
    return buf;
}

// The press test's lines, one per source: "ONVIF: arrived right away", "Email: arrived after 14 seconds", "ONVIF: didn't
// arrive", or while the test is open "Email: not yet".
inline std::vector<std::string> press_test_lines(const doorbell_view& v)
{
    std::vector<std::string> out;
    for(auto& s : v.test_sources)
    {
        std::string line = press_source_name(s.source) + ": ";
        if(s.seen && s.after_ms < 1000)
            line += "arrived right away";
        else if(s.seen)
        {
            const int64_t secs = (s.after_ms + 500) / 1000;
            line += "arrived after " + std::to_string(secs) + (secs == 1 ? " second" : " seconds");
        }
        else
            line += v.test_open ? "not yet" : "didn't arrive";
        out.push_back(line);
    }
    return out;
}

// What a finished press test means for the choice; empty while it runs or before one.
inline std::string press_test_verdict(const doorbell_view& v, const std::string& chosen)
{
    if(!v.test_any || v.test_open)
        return std::string();
    bool onvif = false, email = false;
    for(auto& s : v.test_sources)
    {
        if(s.seen && s.source == "onvif")
            onvif = true;
        else if(s.seen)
            email = true;
    }
    if(onvif)
        return chosen == "onvif" ? "The doorbell sent the press over ONVIF. Rings will arrive right away."
                                 : "The doorbell sent the press over ONVIF. Choose ONVIF so rings arrive right away.";
    if(email)
        return "The doorbell only emailed the press. Keep Reolink email.";
    return chosen == "onvif" ? "No press arrived. If this is a Reolink doorbell, choose Reolink email and set it up."
                             : "No press arrived. Check the email settings in the Reolink app, then test again.";
}

// How the doorbell's email is getting through, a line at a time.
inline std::vector<std::string> mail_status_lines(const doorbell_view& v, const std::string& chosen)
{
    std::vector<std::string> out;
    if(chosen != "reolink_email")
        return out;
    if(v.saved_source != "reolink_email")
        out.push_back("Save to start receiving this doorbell's email.");
    else if(!v.listening)
        out.push_back(v.last_error.empty() ? std::string("Revere is getting ready to receive the doorbell's email. This takes a few seconds.")
                                           : "Revere can't receive the doorbell's email: " + v.last_error);
    else if(v.last_email_ms)
        out.push_back("The doorbell last emailed a press at " + clock_text(v.last_email_ms) + ".");
    else
        out.push_back("Ready. The doorbell hasn't emailed a press yet. Press it once to check.");
    if(v.other_mail)
        out.push_back("The doorbell sent " + std::to_string(v.other_mail) + (v.other_mail == 1 ? " email that wasn't a press." : " emails that weren't presses.") +
                      " In the Reolink app, set the email Alarm Schedule to Ring only.");
    if(v.bad_logins)
        out.push_back("The doorbell's email login failed " + std::to_string(v.bad_logins) + (v.bad_logins == 1 ? " time." : " times.") +
                      " Check the password in the Reolink app.");
    return out;
}

}

#endif
