#include "r_vss/r_detection_cap.h"
#include <algorithm>
#include <map>
#include <set>

using namespace std;

namespace
{

struct frame
{
    int64_t ts {0};
    vector<size_t> members;
    float score {0};
    size_t persons {0};
};

float score_of(const r_vss::r_capped_detection& d)
{
    return d.area > 0 ? d.area * d.confidence : d.confidence;
}

}

vector<bool> r_vss::cap_event_detections(const vector<r_capped_detection>& detections, size_t max_detections)
{
    vector<bool> keep(detections.size(), true);
    if(detections.size() <= max_detections)
        return keep;

    // Frames in ts order.
    map<int64_t, frame> by_ts;
    for(size_t i = 0; i < detections.size(); ++i)
    {
        auto& f = by_ts[detections[i].ts];
        f.ts = detections[i].ts;
        f.members.push_back(i);
        f.score = max(f.score, score_of(detections[i]));
        if(detections[i].class_name == "person")
            ++f.persons;
    }
    vector<frame> frames;
    for(auto& [ts, f] : by_ts)
        frames.push_back(std::move(f));

    set<size_t> must;
    for(size_t i = 0; i < min(frames.size(), EVENT_DETECTIONS_FIRST_FRAMES); ++i)
        must.insert(i);
    must.insert(frames.size() - 1);

    // The most confident detection's frame, each class's best frame and the
    // frame with the most people (the earliest on a tie).
    size_t most_confident = 0, most_people = 0;
    float best_confidence = -1;
    map<string, pair<float, size_t>> best_of_class;
    for(size_t fi = 0; fi < frames.size(); ++fi)
    {
        for(auto i : frames[fi].members)
        {
            const auto& d = detections[i];
            if(d.confidence > best_confidence)
            {
                best_confidence = d.confidence;
                most_confident = fi;
            }
            auto it = best_of_class.find(d.class_name);
            if(it == best_of_class.end() || score_of(d) > it->second.first)
                best_of_class[d.class_name] = {score_of(d), fi};
        }
        if(frames[fi].persons > frames[most_people].persons)
            most_people = fi;
    }
    must.insert(most_confident);
    if(frames[most_people].persons > 0)
        must.insert(most_people);
    for(auto& [cls, best] : best_of_class)
        must.insert(best.second);

    vector<bool> kept_frame(frames.size(), false);
    size_t kept = 0;
    for(auto fi : must)
    {
        kept_frame[fi] = true;
        kept += frames[fi].members.size();
    }

    // The rest, best first (the earlier on a tie), while they fit.
    vector<size_t> rest;
    for(size_t fi = 0; fi < frames.size(); ++fi)
        if(!kept_frame[fi])
            rest.push_back(fi);
    stable_sort(rest.begin(), rest.end(), [&](size_t a, size_t b){ return frames[a].score > frames[b].score; });
    for(auto fi : rest)
    {
        if(kept + frames[fi].members.size() > max_detections)
            continue;
        kept_frame[fi] = true;
        kept += frames[fi].members.size();
    }

    for(size_t fi = 0; fi < frames.size(); ++fi)
        if(!kept_frame[fi])
            for(auto i : frames[fi].members)
                keep[i] = false;
    return keep;
}
