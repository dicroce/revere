#include "tray_status.h"
#include "r_utils/3rdparty/json/json.h"
#include <vector>

using namespace std;

static string _cameras_word(size_t n)
{
    return (n == 1) ? "camera" : "cameras";
}

static string _name_list(const vector<string>& names)
{
    const size_t shown_max = 2;
    string out;
    for(size_t i = 0; i < names.size() && i < shown_max; ++i)
    {
        if(i > 0)
            out += ", ";
        out += names[i];
    }
    if(names.size() > shown_max)
        out += " and " + to_string(names.size() - shown_max) + " more";
    return out;
}

revere::tray_status revere::summarize_cameras(const optional<string>& cameras_json)
{
    tray_status st;

    if(!cameras_json)
    {
        st.problem = true;
        st.summary = "Revere service isn't running";
        st.detail = "Nothing is being recorded";
        return st;
    }

    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(*cameras_json);
    }
    catch(const exception&)
    {
        st.reachable = true;
        st.problem = true;
        st.summary = "Revere is running";
        st.detail = "Couldn't read camera status";
        return st;
    }

    st.reachable = true;

    size_t assigned = 0, recording = 0;
    vector<string> failed, starting;
    if(j.contains("cameras") && j["cameras"].is_array())
    {
        for(const auto& c : j["cameras"])
        {
            if(c.value("state", string()) != "assigned")
                continue;
            ++assigned;

            auto name = c.value("friendly_name", string());
            if(name.empty())
                name = c.value("camera_name", string("camera"));

            if(c.value("stream_failed", false))
                failed.push_back(name);
            else if(c.value("receiving_video", false))
                ++recording;
            else
                starting.push_back(name);
        }
    }

    if(assigned == 0)
    {
        st.summary = "Revere is running";
        st.detail = "No cameras set up yet";
        return st;
    }

    if(recording == assigned)
        st.summary = (assigned == 1) ? "Recording 1 camera" : "Recording all " + to_string(assigned) + " cameras";
    else
        st.summary = "Recording " + to_string(recording) + " of " + to_string(assigned) + " " + _cameras_word(assigned);

    if(!failed.empty())
    {
        st.problem = true;
        st.detail = "Not recording: " + _name_list(failed);
    }
    else if(!starting.empty())
        st.detail = "Connecting: " + _name_list(starting);

    return st;
}
