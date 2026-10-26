#include "test_r_detection_cap.h"
#include "r_vss/r_detection_cap.h"
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace r_vss;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_detection_cap);

static const int64_t T0 = 1761259000000LL;

// Kept or dropped as whole frames.
static bool frames_whole(const vector<r_capped_detection>& dets, const vector<bool>& keep)
{
    map<int64_t, set<bool>> by_ts;
    for(size_t i = 0; i < dets.size(); ++i)
        by_ts[dets[i].ts].insert(keep[i]);
    for(auto& [ts, states] : by_ts)
        if(states.size() != 1)
            return false;
    return true;
}

static bool frame_kept(const vector<r_capped_detection>& dets, const vector<bool>& keep, int64_t ts)
{
    for(size_t i = 0; i < dets.size(); ++i)
        if(dets[i].ts == ts)
            return keep[i];
    return false;
}

void test_r_detection_cap::test_under_the_cap_keeps_all()
{
    vector<r_capped_detection> dets;
    for(int i = 0; i < (int)MAX_EVENT_DETECTIONS; ++i)
        dets.push_back({T0 + i * 1000, "person", 0.7f, 100.f});
    auto keep = cap_event_detections(dets);
    RTF_ASSERT(keep.size() == dets.size());
    RTF_ASSERT(count(keep.begin(), keep.end(), true) == (long)dets.size());
    RTF_ASSERT(cap_event_detections({}).empty());
}

// An hour-long event, a person a second: about 3,600 detections.
void test_r_detection_cap::test_long_busy_event()
{
    vector<r_capped_detection> dets;
    const int n = 3600;
    for(int i = 0; i < n; ++i)
    {
        // Scores vary; none of these stand out.
        float area = 1000.f + (float)((i * 37) % 500);
        dets.push_back({T0 + i * 1000, "person", 0.70f, area});
    }
    const int64_t three_people = T0 + 1800 * 1000;   // three people in one frame
    dets.push_back({three_people, "person", 0.70f, 900.f});
    dets.push_back({three_people, "person", 0.70f, 900.f});
    const int64_t car = T0 + 2000 * 1000;            // the only car: small and faint
    dets.push_back({car, "car", 0.66f, 50.f});
    const int64_t peak = T0 + 2500 * 1000;           // the most confident detection, small
    dets[2500].confidence = 0.95f;
    dets[2500].area = 10.f;
    const int64_t biggest = T0 + 3000 * 1000;        // the best area x confidence
    dets[3000].area = 50000.f;

    auto keep = cap_event_detections(dets);
    const long kept = count(keep.begin(), keep.end(), true);
    RTF_ASSERT(kept <= (long)MAX_EVENT_DETECTIONS && kept >= (long)MAX_EVENT_DETECTIONS - 3);
    RTF_ASSERT(frames_whole(dets, keep));

    // Always kept.
    for(int i = 0; i < (int)EVENT_DETECTIONS_FIRST_FRAMES; ++i)
        RTF_ASSERT(frame_kept(dets, keep, T0 + i * 1000));
    RTF_ASSERT(frame_kept(dets, keep, T0 + (n - 1) * 1000));
    RTF_ASSERT(frame_kept(dets, keep, peak));
    RTF_ASSERT(frame_kept(dets, keep, car));
    RTF_ASSERT(frame_kept(dets, keep, three_people));
    RTF_ASSERT(frame_kept(dets, keep, biggest));

    // The rest are the best-scoring frames: nothing dropped outscores anything
    // kept for its score.
    float min_kept = 1e9f, max_dropped = 0.f;
    set<int64_t> forced = {peak, car, three_people, T0 + (n - 1) * 1000};
    for(int i = 0; i < (int)EVENT_DETECTIONS_FIRST_FRAMES; ++i)
        forced.insert(T0 + i * 1000);
    for(size_t i = 0; i < dets.size(); ++i)
    {
        if(forced.count(dets[i].ts))
            continue;
        float s = dets[i].area * dets[i].confidence;
        if(keep[i])
            min_kept = min(min_kept, s);
        else max_dropped = max(max_dropped, s);
    }
    RTF_ASSERT(max_dropped <= min_kept);
}

// Frames of two detections, later frames scoring higher. Forced: the 6 first
// frames and the 6 best (the last 6, which include the last and the most
// confident): 24 detections, over a budget of 9, and nothing else is added.
void test_r_detection_cap::test_whole_frames_within_budget()
{
    vector<r_capped_detection> dets;
    for(int i = 0; i < 20; ++i)
    {
        float conf = 0.5f + 0.01f * (float)i;
        dets.push_back({T0 + i * 1000, "person", conf, 0.f});
        dets.push_back({T0 + i * 1000, "person", conf, 0.f});
    }
    auto keep = cap_event_detections(dets, 9);
    RTF_ASSERT(frames_whole(dets, keep));
    RTF_ASSERT(count(keep.begin(), keep.end(), true) == 24);
    for(int i = 0; i < 6; ++i)
        RTF_ASSERT(frame_kept(dets, keep, T0 + i * 1000));
    for(int i = 14; i < 20; ++i)
        RTF_ASSERT(frame_kept(dets, keep, T0 + i * 1000));
    RTF_ASSERT(!frame_kept(dets, keep, T0 + 13 * 1000));

    // With room for two more frames, the two best-scoring others are added.
    keep = cap_event_detections(dets, 28);
    RTF_ASSERT(count(keep.begin(), keep.end(), true) == 28);
    RTF_ASSERT(frame_kept(dets, keep, T0 + 13 * 1000) && frame_kept(dets, keep, T0 + 12 * 1000));
    RTF_ASSERT(!frame_kept(dets, keep, T0 + 11 * 1000));
}

// Budget 20. Forced: 6 first frames, 6 best (score 0.9) and the last, 13
// detections. Next best is a 10-detection frame at 0.8 that doesn't fit; the
// 1-detection frames at 0.5 after it still go in until the budget is full.
void test_r_detection_cap::test_smaller_frames_fill_past_a_big_one()
{
    vector<r_capped_detection> dets;
    int64_t t = T0;
    for(int i = 0; i < 6; ++i, t += 1000)
        dets.push_back({t, "bicycle", 0.1f, 0.f});
    for(int i = 0; i < 6; ++i, t += 1000)
        dets.push_back({t, "bicycle", 0.9f, 0.f});
    const int64_t big = t;
    for(int i = 0; i < 10; ++i)
        dets.push_back({big, "bicycle", 0.8f, 0.f});
    t += 1000;
    vector<int64_t> small;
    for(int i = 0; i < 10; ++i, t += 1000)
    {
        small.push_back(t);
        dets.push_back({t, "bicycle", 0.5f, 0.f});
    }
    dets.push_back({t, "bicycle", 0.1f, 0.f});

    auto keep = cap_event_detections(dets, 20);
    RTF_ASSERT(frames_whole(dets, keep));
    RTF_ASSERT(count(keep.begin(), keep.end(), true) == 20);
    RTF_ASSERT(!frame_kept(dets, keep, big));
    for(int i = 0; i < 7; ++i)
        RTF_ASSERT(frame_kept(dets, keep, small[i]));
    RTF_ASSERT(!frame_kept(dets, keep, small[7]));
}

// The 6 best-scoring frames are kept even when they alone are over budget.
void test_r_detection_cap::test_best_frames_kept_whatever_their_size()
{
    vector<r_capped_detection> dets;
    int64_t t = T0;
    for(int i = 0; i < 6; ++i, t += 1000)
        dets.push_back({t, "bicycle", 0.1f, 0.f});
    vector<int64_t> best;
    for(int i = 0; i < 8; ++i, t += 1000)
    {
        const float conf = (i < 6) ? 0.9f - 0.01f * (float)i : 0.5f;
        if(i < 6)
            best.push_back(t);
        for(int j = 0; j < 3; ++j)
            dets.push_back({t, "bicycle", conf, 0.f});
    }
    dets.push_back({t, "bicycle", 0.1f, 0.f});

    auto keep = cap_event_detections(dets, 10);
    RTF_ASSERT(frames_whole(dets, keep));
    for(auto ts : best)
        RTF_ASSERT(frame_kept(dets, keep, ts));
    // 6 first + 6 best x 3 + the last; nothing else fits.
    RTF_ASSERT(count(keep.begin(), keep.end(), true) == 6 + 18 + 1);
}
