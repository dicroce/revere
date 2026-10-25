#include "r_vss/r_event_timing.h"
#include "r_utils/r_time_utils.h"
#include <chrono>

using namespace std;

int64_t r_vss::wall_clock_ms()
{
    return r_utils::r_time_utils::tp_to_epoch_millis(chrono::system_clock::now());
}

void r_vss::add_look(r_event_timing& timing, const r_yolo_look& look)
{
    if(timing.looks.size() >= MAX_EVENT_LOOKS)
    {
        timing.looks.erase(timing.looks.begin() + MAX_EVENT_LOOKS / 2);
        ++timing.looks_dropped;
    }
    timing.looks.push_back(look);
}

string r_vss::event_timing_json_members(const r_event_timing& timing, int64_t written_at)
{
    string out;
    const auto member = [&out](const char* name, int64_t v) {
        if(v > 0)
            out += string(", \"") + name + "\": " + to_string(v);
    };
    member("confirmed_at", timing.confirmed_at);
    member("ended_at", timing.ended_at);
    member("written_at", written_at);

    if(!timing.looks.empty())
    {
        out += ", \"looks\": [";
        for(size_t i = 0; i < timing.looks.size(); ++i)
        {
            const auto& l = timing.looks[i];
            if(i > 0)
                out += ", ";
            out += "{\"ts\": " + to_string(l.ts) + ", \"inferred_at\": " + to_string(l.inferred_at) +
                   ", \"n_dets\": " + to_string(l.n_dets) + "}";
        }
        out += "]";
    }
    if(timing.looks_dropped > 0)
        out += ", \"looks_dropped\": " + to_string(timing.looks_dropped);
    return out;
}
