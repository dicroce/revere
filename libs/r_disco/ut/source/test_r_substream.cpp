#include "test_r_substream.h"
#include "r_disco/r_substream.h"
#include <map>
#include <stdexcept>

using namespace std;
using namespace r_disco;
using namespace r_onvif;
using namespace r_pipeline;

REGISTER_TEST_FIXTURE(test_r_substream);

static onvif_profile_info _profile(const string& token, uint16_t w, uint16_t h, const string& encoding = "H264")
{
    onvif_profile_info p;
    p.token = token;
    p.encoding = encoding;
    p.width = w;
    p.height = h;
    p.video_source_token = "VideoSource_1";
    return p;
}

// What a usable substream's probe sees: 15 fps H.264, a keyframe every second, no B-frames.
static r_stream_probe_result _good(uint16_t w, uint16_t h)
{
    r_stream_probe_result r;
    r.answered = true;
    r.received_video = true;
    r.encoding = H264_ENCODING;
    r.width = w;
    r.height = h;
    r.frames = 45;
    r.key_frames = 3;
    r.fps = 15.0;
    r.key_interval_ms = 1000;
    return r;
}

// A stand-in camera: each profile's URL, each URL's probe, and the URLs it was asked to probe, in order.
struct fake_camera
{
    map<string, string> uris;
    map<string, r_stream_probe_result> probes;
    vector<string> probed;
    vector<string> asked_uri;

    r_substream_camera ops()
    {
        r_substream_camera c;
        c.stream_uri = [this](const onvif_profile_token& token) -> string {
            asked_uri.push_back(token);
            auto it = uris.find(token);
            if(it == uris.end())
                throw runtime_error("GetStreamUri failed");
            return it->second;
        };
        c.probe = [this](const string& url) {
            probed.push_back(url);
            auto it = probes.find(url);
            return it == probes.end() ? r_stream_probe_result() : it->second;
        };
        return c;
    }
};

// A 2560x1920 main with a 640x480 and a 1280x960 profile of the same sensor (640x480 ranks first).
static vector<onvif_profile_info> _reolink()
{
    return {_profile("Main", 2560, 1920), _profile("Sub", 640, 480), _profile("Mid", 1280, 960)};
}

void test_r_substream::test_picks_the_best_candidate_that_passes()
{
    fake_camera cam;
    cam.uris = {{"Sub", "rtsp://cam/sub"}, {"Mid", "rtsp://cam/mid"}};
    cam.probes = {{"rtsp://cam/sub", _good(640, 480)}, {"rtsp://cam/mid", _good(1280, 960)}};
    auto profiles = _reolink();
    auto c = find_substream(profiles, profiles[0], 2560, 1920, cam.ops());
    RTF_ASSERT(c.found);
    RTF_ASSERT(c.reason.empty());
    RTF_ASSERT(c.profile.token == "Sub");
    RTF_ASSERT(c.rtsp_url == "rtsp://cam/sub");
    RTF_ASSERT_EQUAL(c.probe.width, 640);
    RTF_ASSERT_EQUAL(c.probe.key_interval_ms, 1000);
    RTF_ASSERT(!c.anamorphic);
    // Only the one it chose was opened.
    RTF_ASSERT(cam.probed == vector<string>{"rtsp://cam/sub"});
}

void test_r_substream::test_tries_the_next_when_the_best_fails()
{
    fake_camera cam;
    cam.uris = {{"Sub", "rtsp://cam/sub"}, {"Mid", "rtsp://cam/mid"}};
    auto b_frames = _good(640, 480);
    b_frames.b_slices = true;
    cam.probes = {{"rtsp://cam/sub", b_frames}, {"rtsp://cam/mid", _good(1280, 960)}};
    auto profiles = _reolink();
    auto c = find_substream(profiles, profiles[0], 2560, 1920, cam.ops());
    RTF_ASSERT(c.found);
    RTF_ASSERT(c.profile.token == "Mid");
    RTF_ASSERT(c.rtsp_url == "rtsp://cam/mid");
    RTF_ASSERT((cam.probed == vector<string>{"rtsp://cam/sub", "rtsp://cam/mid"}));
}

void test_r_substream::test_none_gives_the_best_candidates_reason()
{
    fake_camera cam;
    cam.uris = {{"Sub", "rtsp://cam/sub"}, {"Mid", "rtsp://cam/mid"}};
    auto b_frames = _good(640, 480);
    b_frames.b_slices = true;
    auto slow = _good(1280, 960);
    slow.fps = 2.0;
    cam.probes = {{"rtsp://cam/sub", b_frames}, {"rtsp://cam/mid", slow}};
    auto profiles = _reolink();
    auto c = find_substream(profiles, profiles[0], 2560, 1920, cam.ops());
    RTF_ASSERT(!c.found);
    RTF_ASSERT(c.reason == "uses B-frames");
    RTF_ASSERT(c.rtsp_url.empty());
}

void test_r_substream::test_no_candidates_opens_nothing()
{
    // A single stream, and one that's too big to be a substream: nothing to try.
    fake_camera cam;
    vector<onvif_profile_info> profiles = {_profile("Main", 1920, 1080), _profile("Big", 1920, 1080)};
    auto c = find_substream(profiles, profiles[0], 1920, 1080, cam.ops());
    RTF_ASSERT(!c.found);
    RTF_ASSERT(c.reason == "no smaller stream");
    RTF_ASSERT(cam.asked_uri.empty());
    RTF_ASSERT(cam.probed.empty());
}

void test_r_substream::test_no_stream_address()
{
    // The best candidate's address can't be had (GetStreamUri throws); the next one is a stream the camera never answers on.
    fake_camera cam;
    cam.uris = {{"Mid", "rtsp://cam/mid"}};
    auto profiles = _reolink();
    auto c = find_substream(profiles, profiles[0], 2560, 1920, cam.ops());
    RTF_ASSERT(!c.found);
    RTF_ASSERT(c.reason == "no stream address");
    RTF_ASSERT((cam.asked_uri == vector<string>{"Sub", "Mid"}));
    RTF_ASSERT(cam.probed == vector<string>{"rtsp://cam/mid"});

    // An empty address counts the same, and isn't probed.
    fake_camera blank;
    blank.uris = {{"Sub", ""}, {"Mid", ""}};
    auto b = find_substream(profiles, profiles[0], 2560, 1920, blank.ops());
    RTF_ASSERT(b.reason == "no stream address");
    RTF_ASSERT(blank.probed.empty());
}

void test_r_substream::test_probes_at_most_the_limit()
{
    // Three candidates, none usable: only SUBSTREAM_MAX_PROBES are opened.
    fake_camera cam;
    auto profiles = _reolink();
    profiles.push_back(_profile("Small", 352, 288));
    for(auto& p : profiles)
        cam.uris[p.token] = "rtsp://cam/" + p.token;
    auto c = find_substream(profiles, profiles[0], 2560, 1920, cam.ops());
    RTF_ASSERT(!c.found);
    RTF_ASSERT(c.reason == "camera didn't answer");
    RTF_ASSERT_EQUAL((int)cam.probed.size(), SUBSTREAM_MAX_PROBES);
}

void test_r_substream::test_squeezed_picture_is_kept()
{
    // A 16:9 main with a 704x576 substream of the same view: usable, and marked as squeezed.
    fake_camera cam;
    cam.uris = {{"Sub", "rtsp://cam/sub"}};
    cam.probes = {{"rtsp://cam/sub", _good(704, 576)}};
    vector<onvif_profile_info> profiles = {_profile("Main", 1920, 1080), _profile("Sub", 704, 576)};
    auto c = find_substream(profiles, profiles[0], 1920, 1080, cam.ops());
    RTF_ASSERT(c.found);
    RTF_ASSERT(c.anamorphic);
}
