#include "test_r_substream.h"
#include "r_disco/r_substream.h"
#include "r_pipeline/r_stream_info.h"
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

// What a found substream leaves on the camera: on, ok, and the profile, address, codec and the size the SPS gave.
void test_r_substream::test_set_records_the_pick()
{
    r_substream_choice choice;
    choice.found = true;
    choice.profile = _profile("sub_1", 704, 396);  // what ONVIF said
    choice.rtsp_url = "rtsp://10.0.0.5/sub";
    choice.probe = _good(640, 352);                 // what the stream really is
    choice.probe.key_interval_ms = 2000;

    r_camera camera;
    substream_clear(camera, "no smaller stream");  // an earlier answer is replaced
    substream_set(camera, choice, "H264", "sprop-parameter-sets=AAAA,BBBB", 90000);

    RTF_ASSERT(substream_on(camera));
    RTF_ASSERT(camera.sub_status.value() == "ok");
    RTF_ASSERT(camera.sub_onvif_profile_token.value() == "sub_1");
    RTF_ASSERT(camera.sub_rtsp_url.value() == "rtsp://10.0.0.5/sub");
    RTF_ASSERT(camera.sub_video_codec.value() == "H264");
    RTF_ASSERT(camera.sub_video_codec_parameters.value() == "sprop-parameter-sets=AAAA,BBBB");
    RTF_ASSERT_EQUAL(camera.sub_video_timebase.value(), 90000);
    RTF_ASSERT_EQUAL(camera.sub_width.value(), 640);
    RTF_ASSERT_EQUAL(camera.sub_height.value(), 352);  // the stream's own size, not the profile's
    RTF_ASSERT_EQUAL(camera.sub_key_interval_ms.value(), 2000);

    // A probe that saw one keyframe leaves the interval unset, not -1.
    choice.probe.key_interval_ms = -1;
    substream_set(camera, choice, "H264", "x", 90000);
    RTF_ASSERT(camera.sub_key_interval_ms.is_null());
}

// No substream: off, the reason in the settings' words, and nothing left of an earlier pick.
void test_r_substream::test_clear_goes_back_to_main()
{
    r_substream_choice choice;
    choice.found = true;
    choice.profile = _profile("sub_1", 640, 360);
    choice.rtsp_url = "rtsp://10.0.0.5/sub";
    choice.probe = _good(640, 360);

    r_camera camera;
    substream_set(camera, choice, "H264", "p", 90000);
    substream_clear(camera, "uses B-frames");

    RTF_ASSERT(!substream_on(camera));
    RTF_ASSERT(camera.sub_mode.value() == "off");
    RTF_ASSERT(camera.sub_status.value() == "none: uses B-frames");
    RTF_ASSERT(camera.sub_onvif_profile_token.is_null());
    RTF_ASSERT(camera.sub_rtsp_url.is_null());
    RTF_ASSERT(camera.sub_video_codec.is_null());
    RTF_ASSERT(camera.sub_video_codec_parameters.is_null());
    RTF_ASSERT(camera.sub_video_timebase.is_null());
    RTF_ASSERT(camera.sub_width.is_null());
    RTF_ASSERT(camera.sub_height.is_null());
    RTF_ASSERT(camera.sub_key_interval_ms.is_null());
}

// A copy takes every substream field and nothing else of the camera.
void test_r_substream::test_copy_takes_only_the_substream()
{
    r_substream_choice choice;
    choice.found = true;
    choice.profile = _profile("sub_1", 640, 360);
    choice.rtsp_url = "rtsp://10.0.0.5/sub";
    choice.probe = _good(640, 360);
    r_camera from;
    from.friendly_name = string("from");
    substream_set(from, choice, "H264", "p", 90000);

    r_camera to;
    to.friendly_name = string("to");
    substream_clear(to, "uses B-frames");
    substream_copy(from, to);

    RTF_ASSERT(to.friendly_name.value() == "to");
    RTF_ASSERT(substream_on(to));
    RTF_ASSERT(to.sub_status.value() == "ok");
    RTF_ASSERT(to.sub_rtsp_url.value() == "rtsp://10.0.0.5/sub");
    RTF_ASSERT_EQUAL(to.sub_width.value(), 640);
    RTF_ASSERT_EQUAL(to.sub_key_interval_ms.value(), 1000);

    // And a "none" copies over a pick: the camera goes back to main with the reason.
    r_camera none;
    substream_clear(none, "no smaller stream");
    substream_copy(none, to);
    RTF_ASSERT(!substream_on(to));
    RTF_ASSERT(to.sub_status.value() == "none: no smaller stream");
    RTF_ASSERT(to.sub_rtsp_url.is_null());
}

// The camera moved: the same stream at a new address is followed; a changed codec or size drops it, with the reason.
void test_r_substream::test_follow_keeps_the_same_stream()
{
    const string params = "sprop-parameter-sets=Z2QACqzZRifmwFqAgICgAAB9IAAXcAHiRLLA,aOvjyyLA";
    auto size = main_picture_size("H264", params);

    r_substream_choice choice;
    choice.found = true;
    choice.profile = _profile("sub_1", size.first, size.second);
    choice.rtsp_url = "rtsp://10.0.0.5/sub";
    choice.probe = _good(size.first, size.second);

    r_camera camera;
    substream_set(camera, choice, "H264", params, 90000);

    // Same stream, new address (and the SPS bytes may differ).
    RTF_ASSERT(substream_follow(camera, "rtsp://10.0.0.9/sub", "H264", params));
    RTF_ASSERT(substream_on(camera));
    RTF_ASSERT(camera.sub_rtsp_url.value() == "rtsp://10.0.0.9/sub");

    // The camera now sends H.265 there: not the stream that was checked.
    RTF_ASSERT(!substream_follow(camera, "rtsp://10.0.0.9/sub", "H265", "sprop-sps=AAAA"));
    RTF_ASSERT(!substream_on(camera));
    RTF_ASSERT(camera.sub_status.value() == "none: camera changed the stream");
    RTF_ASSERT(camera.sub_rtsp_url.is_null());

    // A substream that is off isn't touched.
    r_camera off;
    substream_clear(off, "no smaller stream");
    RTF_ASSERT(substream_follow(off, "rtsp://10.0.0.9/sub", "H264", params));
    RTF_ASSERT(off.sub_status.value() == "none: no smaller stream");
    RTF_ASSERT(off.sub_rtsp_url.is_null());
}

// A different picture size under the same codec is a different stream too.
void test_r_substream::test_follow_drops_a_resized_stream()
{
    const string params = "sprop-parameter-sets=Z2QACqzZRifmwFqAgICgAAB9IAAXcAHiRLLA,aOvjyyLA";
    auto size = main_picture_size("H264", params);

    r_substream_choice choice;
    choice.found = true;
    choice.profile = _profile("sub_1", size.first, size.second);
    choice.rtsp_url = "rtsp://10.0.0.5/sub";
    choice.probe = _good(size.first, size.second);

    r_camera camera;
    substream_set(camera, choice, "H264", params, 90000);
    camera.sub_width.set_value(size.first / 2);  // as if it had been stored at half the size

    RTF_ASSERT(!substream_follow(camera, "rtsp://10.0.0.9/sub", "H264", params));
    RTF_ASSERT(camera.sub_status.value() == "none: camera changed the stream");
}

// A camera whose description carries no SPS (no sprop) can't say its size: that isn't a change. The address follows,
// the stored parameters (which have the SPS) stay, and a different codec is still a change.
void test_r_substream::test_follow_without_an_sps_is_not_a_change()
{
    const string params = "sprop-parameter-sets=Z2QACqzZRifmwFqAgICgAAB9IAAXcAHiRLLA,aOvjyyLA";
    auto size = main_picture_size("H264", params);

    r_substream_choice choice;
    choice.found = true;
    choice.profile = _profile("sub_1", size.first, size.second);
    choice.rtsp_url = "rtsp://10.0.0.5/sub";
    choice.probe = _good(size.first, size.second);

    r_camera camera;
    substream_set(camera, choice, "H264", params, 90000);

    RTF_ASSERT(substream_follow(camera, "rtsp://10.0.0.9/sub", "H264", "packetization-mode=1"));
    RTF_ASSERT(substream_on(camera));
    RTF_ASSERT(camera.sub_rtsp_url.value() == "rtsp://10.0.0.9/sub");
    RTF_ASSERT(camera.sub_video_codec_parameters.value() == params);
    RTF_ASSERT_EQUAL(camera.sub_width.value(), size.first);

    RTF_ASSERT(!substream_follow(camera, "rtsp://10.0.0.9/sub", "H265", ""));
    RTF_ASSERT(!substream_on(camera));
}

// Which profile is main: the one with the stored token; else the largest, the first of equals.
void test_r_substream::test_main_profile_index()
{
    vector<onvif_profile_info> profiles = {
        _profile("small", 640, 480),
        _profile("big_a", 2560, 1920),
        _profile("big_b", 2560, 1920),
        _profile("mid", 1280, 720)
    };
    r_utils::r_nullable<string> none;
    RTF_ASSERT_EQUAL(main_profile_index(profiles, none), (size_t)1);      // the largest; a tie goes to the first
    RTF_ASSERT_EQUAL(main_profile_index(profiles, r_utils::r_nullable<string>(string("mid"))), (size_t)3);   // the stored token
    RTF_ASSERT_EQUAL(main_profile_index(profiles, r_utils::r_nullable<string>(string("small"))), (size_t)0);
    RTF_ASSERT_EQUAL(main_profile_index(profiles, r_utils::r_nullable<string>(string("gone"))), (size_t)1);  // not there: the largest
    RTF_ASSERT_EQUAL(main_profile_index({}, none), (size_t)0);
}

// Main's size comes from the SPS in its codec parameters, for either codec; junk reads as 0 x 0.
void test_r_substream::test_main_picture_size()
{
    const string params = "sprop-parameter-sets=Z2QACqzZRifmwFqAgICgAAB9IAAXcAHiRLLA,aOvjyyLA";
    auto expected = parse_h264_sps(get_h264_sps(params).value());
    RTF_ASSERT(expected.width > 0 && expected.height > 0);

    auto size = main_picture_size("H264", params);
    RTF_ASSERT_EQUAL(size.first, expected.width);
    RTF_ASSERT_EQUAL(size.second, expected.height);

    // An H.265 stream's size comes from its own SPS (a 640 x 360 one, from x265).
    auto h265 = main_picture_size("H265", "sprop-sps=QgEBBAgAAAMAnggAAAMAAD+QAKBALSyys0kmV4C3AgIABAAAAwAEAAADADwg");
    RTF_ASSERT_EQUAL(h265.first, 640);
    RTF_ASSERT_EQUAL(h265.second, 360);
    RTF_ASSERT_EQUAL(main_picture_size("H264", "sprop-sps=QgEBBAgAAAMAnggAAAMAAD+QAKBALSyys0kmV4C3AgIABAAAAwAEAAADADwg").first, 0);

    RTF_ASSERT_EQUAL(main_picture_size("H264", "").first, 0);
    RTF_ASSERT_EQUAL(main_picture_size("H264", "nothing here").second, 0);
    RTF_ASSERT_EQUAL(main_picture_size("MJPEG", params).first, 0);
    RTF_ASSERT_EQUAL(main_picture_size("H265", params).first, 0);  // an H.264 SPS isn't an H.265 one
}
