
#include "test_r_recording_context.h"
#include "r_vss/r_recording_context.h"
#include "r_disco/r_camera.h"
#include "r_vss/r_frame_feeds.h"
#include "r_pipeline/r_gst_source.h"
#include <deque>
#include <vector>

using namespace std;
using namespace r_vss;
using namespace r_disco;

REGISTER_TEST_FIXTURE(test_r_recording_context);

void test_r_recording_context::test_source_name_prefix()
{
    r_camera camera;
    camera.id = "cam-1234";

    // No friendly name: the camera id
    RTF_ASSERT(recording_source_name_prefix(camera) == "cam-1234_");

    camera.friendly_name.set_value(string());
    RTF_ASSERT(recording_source_name_prefix(camera) == "cam-1234_");

    // A friendly name: used as is
    camera.friendly_name.set_value("Raccoon Cam 5");
    RTF_ASSERT(recording_source_name_prefix(camera) == "Raccoon Cam 5_");
}

void test_r_recording_context::test_a_feed_starts_from_the_stamped_gop_cache()
{
    // The cache as the video callback keeps it: each frame pushed before its write, then
    // stamped with the time storage gave it. A re-anchored source (stored ts not pts) keeps
    // storage's time in the burst.
    r_pipeline::gstreamer_init();
    std::deque<_gop_cache_entry> cache;
    auto push = [&](uint8_t v, size_t n, bool key, int64_t pts) {
        std::vector<uint8_t> bytes(n, v);
        cache.push_back({pts, key, r_pipeline::r_gst_buffer(bytes.data(), bytes.size())});
    };
    push(1, 100, true, 10);  stamp_newest_gop_frame(cache, 5010);
    push(2, 50, false, 11);  stamp_newest_gop_frame(cache, 5011);
    push(3, 40, false, 12);  stamp_newest_gop_frame(cache, 5012);

    std::vector<r_pipeline::r_gst_buffer::r_map_info> maps;
    auto gop = gop_for_feeds(cache, maps);
    RTF_ASSERT_EQUAL(gop.size(), (size_t)3);
    RTF_ASSERT(gop[0].key && gop[0].stored && gop[0].ts == 5010 && gop[0].size == 100 && gop[0].data[0] == 1);
    RTF_ASSERT(!gop[2].key && gop[2].ts == 5012 && gop[2].data[0] == 3);

    r_frame_feeds feeds;
    auto id = feeds.subscribe("cam1", "", r_frame_feeds::VIDEO, 0, true);
    feeds.post_video("cam1", "main", gop.back().data, gop.back().size, false, 5012, "h264", "sps1", &gop);
    r_feed_frame f;
    std::vector<int64_t> ts;
    while(feeds.next(id, 0, f) == 1)
        ts.push_back(f.ts);
    RTF_ASSERT((ts == std::vector<int64_t>{5010, 5011, 5012}));

    // A frame whose write threw is never stamped: no GOP, the next key frame.
    push(4, 30, false, 13);
    auto gop2 = gop_for_feeds(cache, maps);
    RTF_ASSERT(!gop2.back().stored);
    auto id2 = feeds.subscribe("cam1", "", r_frame_feeds::VIDEO, 0, true);
    push(5, 30, false, 14);  stamp_newest_gop_frame(cache, 5014);
    auto gop3 = gop_for_feeds(cache, maps);
    feeds.post_video("cam1", "main", gop3.back().data, gop3.back().size, false, 5014, "h264", "sps1", &gop3);
    RTF_ASSERT(feeds.next(id2, 0, f) == 0);
    RTF_ASSERT(!feeds.gop_wanted());
}
