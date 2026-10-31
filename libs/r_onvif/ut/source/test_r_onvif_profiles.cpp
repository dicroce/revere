#include "test_r_onvif_profiles.h"
#include "r_onvif/r_onvif_session.h"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace r_onvif;

REGISTER_TEST_FIXTURE(test_r_onvif_profiles);

// Shaped like the Media1 GetProfilesResponse in the ONVIF Media Service Specification: a dual-lens camera's main
// stream and substream on its first sensor, an MJPEG profile, and a profile on its second sensor.
static const char* DUAL_LENS_PROFILES =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\" "
    "xmlns:tt=\"http://www.onvif.org/ver10/schema\" xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\">"
    "<SOAP-ENV:Body><trt:GetProfilesResponse>"
    "<trt:Profiles token=\"000\" fixed=\"true\"><tt:Name>mainStream</tt:Name>"
    "<tt:VideoSourceConfiguration token=\"vsc0\"><tt:Name>vsc0</tt:Name><tt:UseCount>3</tt:UseCount>"
    "<tt:SourceToken>src0</tt:SourceToken><tt:Bounds x=\"0\" y=\"0\" width=\"2560\" height=\"1920\"/></tt:VideoSourceConfiguration>"
    "<tt:VideoEncoderConfiguration token=\"enc0\"><tt:Name>enc0</tt:Name><tt:UseCount>1</tt:UseCount>"
    "<tt:Encoding>H264</tt:Encoding><tt:Resolution><tt:Width>2560</tt:Width><tt:Height>1920</tt:Height></tt:Resolution>"
    "<tt:Quality>5</tt:Quality><tt:RateControl><tt:FrameRateLimit>15</tt:FrameRateLimit>"
    "<tt:EncodingInterval>1</tt:EncodingInterval><tt:BitrateLimit>6144</tt:BitrateLimit></tt:RateControl>"
    "<tt:H264><tt:GovLength>30</tt:GovLength><tt:H264Profile>High</tt:H264Profile></tt:H264>"
    "</tt:VideoEncoderConfiguration></trt:Profiles>"
    "<trt:Profiles token=\"001\" fixed=\"true\"><tt:Name>subStream</tt:Name>"
    "<tt:VideoSourceConfiguration token=\"vsc0\"><tt:Name>vsc0</tt:Name><tt:UseCount>3</tt:UseCount>"
    "<tt:SourceToken>src0</tt:SourceToken><tt:Bounds x=\"0\" y=\"0\" width=\"2560\" height=\"1920\"/></tt:VideoSourceConfiguration>"
    "<tt:VideoEncoderConfiguration token=\"enc1\"><tt:Name>enc1</tt:Name><tt:UseCount>1</tt:UseCount>"
    "<tt:Encoding>H264</tt:Encoding><tt:Resolution><tt:Width>640</tt:Width><tt:Height>480</tt:Height></tt:Resolution>"
    "<tt:Quality>3</tt:Quality><tt:RateControl><tt:FrameRateLimit>10</tt:FrameRateLimit>"
    "<tt:EncodingInterval>1</tt:EncodingInterval><tt:BitrateLimit>512</tt:BitrateLimit></tt:RateControl>"
    "<tt:H264><tt:GovLength>20</tt:GovLength><tt:H264Profile>Main</tt:H264Profile></tt:H264>"
    "</tt:VideoEncoderConfiguration></trt:Profiles>"
    "<trt:Profiles token=\"002\" fixed=\"true\"><tt:Name>jpeg</tt:Name>"
    "<tt:VideoSourceConfiguration token=\"vsc0\"><tt:Name>vsc0</tt:Name><tt:UseCount>3</tt:UseCount>"
    "<tt:SourceToken>src0</tt:SourceToken><tt:Bounds x=\"0\" y=\"0\" width=\"2560\" height=\"1920\"/></tt:VideoSourceConfiguration>"
    "<tt:VideoEncoderConfiguration token=\"enc2\"><tt:Name>enc2</tt:Name><tt:UseCount>1</tt:UseCount>"
    "<tt:Encoding>JPEG</tt:Encoding><tt:Resolution><tt:Width>640</tt:Width><tt:Height>480</tt:Height></tt:Resolution>"
    "<tt:Quality>3</tt:Quality><tt:RateControl><tt:FrameRateLimit>5</tt:FrameRateLimit>"
    "<tt:EncodingInterval>1</tt:EncodingInterval><tt:BitrateLimit>1024</tt:BitrateLimit></tt:RateControl>"
    "</tt:VideoEncoderConfiguration></trt:Profiles>"
    "<trt:Profiles token=\"100\" fixed=\"true\"><tt:Name>lens2sub</tt:Name>"
    "<tt:VideoSourceConfiguration token=\"vsc1\"><tt:Name>vsc1</tt:Name><tt:UseCount>1</tt:UseCount>"
    "<tt:SourceToken>src1</tt:SourceToken><tt:Bounds x=\"0\" y=\"0\" width=\"1920\" height=\"1080\"/></tt:VideoSourceConfiguration>"
    "<tt:VideoEncoderConfiguration token=\"enc3\"><tt:Name>enc3</tt:Name><tt:UseCount>1</tt:UseCount>"
    "<tt:Encoding>H264</tt:Encoding><tt:Resolution><tt:Width>640</tt:Width><tt:Height>360</tt:Height></tt:Resolution>"
    "<tt:Quality>3</tt:Quality><tt:RateControl><tt:FrameRateLimit>15</tt:FrameRateLimit>"
    "<tt:EncodingInterval>1</tt:EncodingInterval><tt:BitrateLimit>512</tt:BitrateLimit></tt:RateControl>"
    "<tt:H264><tt:GovLength>30</tt:GovLength><tt:H264Profile>Main</tt:H264Profile></tt:H264>"
    "</tt:VideoEncoderConfiguration></trt:Profiles>"
    "</trt:GetProfilesResponse></SOAP-ENV:Body></SOAP-ENV:Envelope>";

// Other prefixes (as gSOAP-based cameras write them), a FrameRateLimit written as a decimal, an H.265 profile with
// no H264 element, and a profile with no encoder configuration at all.
static const char* OTHER_PREFIXES =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<env:Envelope xmlns:env=\"http://www.w3.org/2003/05/soap-envelope\" "
    "xmlns:ns3=\"http://www.onvif.org/ver10/schema\" xmlns:ns4=\"http://www.onvif.org/ver10/media/wsdl\">"
    "<env:Body><ns4:GetProfilesResponse>"
    "<ns4:Profiles token=\"Profile_1\"><ns3:Name>Profile_1</ns3:Name>"
    "<ns3:VideoSourceConfiguration token=\"VideoSource_1\"><ns3:SourceToken>VideoSource_1</ns3:SourceToken></ns3:VideoSourceConfiguration>"
    "<ns3:VideoEncoderConfiguration token=\"VideoEncoder_1\"><ns3:Encoding>H265</ns3:Encoding>"
    "<ns3:Resolution><ns3:Width>3840</ns3:Width><ns3:Height>2160</ns3:Height></ns3:Resolution>"
    "<ns3:RateControl><ns3:FrameRateLimit>25.000000</ns3:FrameRateLimit><ns3:BitrateLimit>8192</ns3:BitrateLimit></ns3:RateControl>"
    "</ns3:VideoEncoderConfiguration></ns4:Profiles>"
    "<ns4:Profiles token=\"Profile_2\"><ns3:Name>audio only</ns3:Name></ns4:Profiles>"
    "</ns4:GetProfilesResponse></env:Body></env:Envelope>";

void test_r_onvif_profiles::test_profile_fields()
{
    auto profiles = parse_profiles_response(DUAL_LENS_PROFILES);
    RTF_ASSERT_EQUAL(profiles.size(), (size_t)4);

    auto& main = profiles[0];
    RTF_ASSERT(main.token == "000");
    RTF_ASSERT(main.encoding == "H264");
    RTF_ASSERT_EQUAL(main.width, 2560);
    RTF_ASSERT_EQUAL(main.height, 1920);
    RTF_ASSERT(main.video_source_token == "src0");
    RTF_ASSERT(main.h264_profile == "High");
    RTF_ASSERT_EQUAL(main.gov_length, 30);
    RTF_ASSERT_EQUAL(main.fps_limit, 15);
    RTF_ASSERT_EQUAL(main.bitrate_limit, 6144);

    auto& sub = profiles[1];
    RTF_ASSERT(sub.token == "001");
    RTF_ASSERT_EQUAL(sub.width, 640);
    RTF_ASSERT_EQUAL(sub.height, 480);
    RTF_ASSERT(sub.h264_profile == "Main");
    RTF_ASSERT_EQUAL(sub.gov_length, 20);
    RTF_ASSERT_EQUAL(sub.fps_limit, 10);
    RTF_ASSERT_EQUAL(sub.bitrate_limit, 512);

    RTF_ASSERT(profiles[2].encoding == "JPEG");
    RTF_ASSERT(profiles[2].h264_profile.empty());
    RTF_ASSERT_EQUAL(profiles[2].gov_length, 0);
    // The source token, not the source configuration's own token.
    RTF_ASSERT(profiles[3].video_source_token == "src1");
}

void test_r_onvif_profiles::test_other_prefixes_and_missing_fields()
{
    auto profiles = parse_profiles_response(OTHER_PREFIXES);
    RTF_ASSERT_EQUAL(profiles.size(), (size_t)2);
    RTF_ASSERT(profiles[0].encoding == "H265");
    RTF_ASSERT_EQUAL(profiles[0].width, 3840);
    RTF_ASSERT_EQUAL(profiles[0].height, 2160);
    RTF_ASSERT_EQUAL(profiles[0].fps_limit, 25);
    RTF_ASSERT_EQUAL(profiles[0].bitrate_limit, 8192);
    RTF_ASSERT(profiles[0].video_source_token == "VideoSource_1");
    RTF_ASSERT(profiles[0].h264_profile.empty());
    RTF_ASSERT_EQUAL(profiles[0].gov_length, 0);

    // Kept, as before, with nothing known about it.
    RTF_ASSERT(profiles[1].token == "Profile_2");
    RTF_ASSERT(profiles[1].encoding == "Unknown");
    RTF_ASSERT_EQUAL(profiles[1].width, 0);
    RTF_ASSERT(profiles[1].video_source_token.empty());
}

void test_r_onvif_profiles::test_unparseable_response_throws()
{
    bool threw = false;
    try { parse_profiles_response("not xml <"); } catch (const runtime_error&) { threw = true; }
    RTF_ASSERT(threw);
    // A response that is XML but has no profiles is no profiles.
    RTF_ASSERT(parse_profiles_response("<a/>").empty());
}

static onvif_profile_info prof(const string& token, const string& source, const string& encoding, uint16_t w, uint16_t h,
                               const string& h264_profile = "", int fps = 0)
{
    onvif_profile_info p;
    p.token = token;
    p.video_source_token = source;
    p.encoding = encoding;
    p.width = w;
    p.height = h;
    p.h264_profile = h264_profile;
    p.fps_limit = fps;
    return p;
}

static vector<string> tokens(const vector<onvif_profile_info>& profiles)
{
    vector<string> out;
    for (auto& p : profiles)
        out.push_back(p.token);
    return out;
}

// A GetProfilesResponse holding one profile per (token, source token, encoding, H.264 profile), written as given.
struct raw_profile
{
    string token;
    string source;
    string encoding;
    string h264_profile;
};

static string profiles_xml(const vector<raw_profile>& profiles)
{
    string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                 "<env:Envelope xmlns:env=\"http://www.w3.org/2003/05/soap-envelope\" "
                 "xmlns:tt=\"http://www.onvif.org/ver10/schema\" xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\">"
                 "<env:Body><trt:GetProfilesResponse>";
    for (auto& p : profiles)
    {
        xml += "<trt:Profiles token=\"" + p.token + "\">"
               "<tt:VideoSourceConfiguration token=\"vsc\"><tt:SourceToken>" + p.source + "</tt:SourceToken></tt:VideoSourceConfiguration>"
               "<tt:VideoEncoderConfiguration token=\"enc\"><tt:Encoding>" + p.encoding + "</tt:Encoding>"
               "<tt:Resolution><tt:Width>640</tt:Width><tt:Height>480</tt:Height></tt:Resolution>"
               "<tt:H264><tt:H264Profile>" + p.h264_profile + "</tt:H264Profile></tt:H264>"
               "</tt:VideoEncoderConfiguration></trt:Profiles>";
    }
    return xml + "</trt:GetProfilesResponse></env:Body></env:Envelope>";
}

void test_r_onvif_profiles::test_profile_token_bounds()
{
    string e_acute = "\xc3\xa9";  // one character, two bytes
    string utf8_64;
    for (int i = 0; i < 64; ++i)
        utf8_64 += e_acute;

    auto profiles = parse_profiles_response(profiles_xml({
        {"", "src", "H264", "Main"},
        {string(64, 'a'), "src", "H264", "Main"},
        {string(65, 'b'), "src", "H264", "Main"},
        {utf8_64, "src", "H264", "Main"},             // 64 characters in 128 bytes
        {utf8_64 + e_acute, "src", "H264", "Main"},
        {"ctl&#x1;", "src", "H264", "Main"},
        {"del&#x7F;", "src", "H264", "Main"},
        {"with space", "src", "H264", "Main"},       // a ReferenceToken may hold a space
    }));
    // The empty, too long and control-character tokens are left out; the rest keep their order.
    RTF_ASSERT(tokens(profiles) == vector<string>({string(64, 'a'), utf8_64, "with space"}));
}

void test_r_onvif_profiles::test_source_token_bounds()
{
    auto profiles = parse_profiles_response(profiles_xml({
        {"ok", "src0", "H264", "Main"},
        {"long", string(65, 's'), "H264", "Main"},
        {"max", string(64, 's'), "H264", "Main"},
        {"ctl", "src&#x1;0", "H264", "Main"},
        {"wrapped", "\n    src0\n  ", "H264", "Main"},
    }));
    RTF_ASSERT_EQUAL(profiles.size(), (size_t)5);
    RTF_ASSERT(profiles[0].video_source_token == "src0");
    // A bad source token reads as empty; the profile is kept.
    RTF_ASSERT(profiles[1].video_source_token.empty());
    RTF_ASSERT(profiles[2].video_source_token == string(64, 's'));
    RTF_ASSERT(profiles[3].video_source_token.empty());
    // Whitespace around the element's text isn't part of the token, so it still matches main's.
    RTF_ASSERT(profiles[4].video_source_token == "src0");
}

void test_r_onvif_profiles::test_enum_bounds()
{
    auto profiles = parse_profiles_response(profiles_xml({
        {"a", "s", string(16, 'E'), string(16, 'P')},
        {"b", "s", string(40, 'E'), string(17, 'P')},
        {"c", "s", " H264\n", string(15, 'P') + "\xc3\xa9"},  // the 16-byte cut would split the last character
    }));
    RTF_ASSERT_EQUAL(profiles.size(), (size_t)3);
    RTF_ASSERT(profiles[0].encoding == string(16, 'E'));
    RTF_ASSERT(profiles[0].h264_profile == string(16, 'P'));
    RTF_ASSERT(profiles[1].encoding == string(16, 'E'));
    RTF_ASSERT(profiles[1].h264_profile == string(16, 'P'));
    RTF_ASSERT(profiles[2].encoding == "H264");
    RTF_ASSERT(profiles[2].h264_profile == string(15, 'P'));
}

void test_r_onvif_profiles::test_substream_is_the_same_sensor()
{
    auto profiles = parse_profiles_response(DUAL_LENS_PROFILES);
    // The MJPEG profile and the other lens's profile are left out.
    RTF_ASSERT(tokens(substream_candidates(profiles, profiles[0])) == vector<string>({"001"}));

    // Main on the second lens: none of the first lens's profiles qualify.
    auto lens2_main = prof("101", "src1", "H264", 1920, 1080);
    profiles.push_back(lens2_main);
    RTF_ASSERT(tokens(substream_candidates(profiles, lens2_main)) == vector<string>({"100"}));
}

void test_r_onvif_profiles::test_substream_codec_and_size()
{
    auto main = prof("main", "s", "H264", 2560, 1440);
    vector<onvif_profile_info> profiles = {
        main,
        prof("h265", "s", "H265", 640, 360),
        prof("lower", "s", "h264", 640, 360),           // the encoding's case doesn't matter
        prof("mpeg4", "s", "MPEG4", 640, 360),
        prof("quarter", "s", "H264", 1280, 720),        // exactly a quarter of main's pixels
        prof("too_big", "s", "H264", 1280, 722),
        prof("narrow", "s", "H264", 319, 240),
        prof("min_width", "s", "H264", 320, 240),
        prof("anamorphic", "s", "H264", 704, 576),      // 4:3-ish of a 16:9 main: allowed
    };
    auto picked = tokens(substream_candidates(profiles, main));
    auto has = [&](const string& t) { return find(picked.begin(), picked.end(), t) != picked.end(); };
    RTF_ASSERT(has("h265"));
    RTF_ASSERT(has("lower"));
    RTF_ASSERT(has("quarter"));
    RTF_ASSERT(has("min_width"));
    RTF_ASSERT(has("anamorphic"));
    RTF_ASSERT(!has("main"));
    RTF_ASSERT(!has("mpeg4"));
    RTF_ASSERT(!has("too_big"));
    RTF_ASSERT(!has("narrow"));
    RTF_ASSERT_EQUAL(picked.size(), (size_t)5);
}

void test_r_onvif_profiles::test_substream_rank()
{
    auto main = prof("main", "s", "H264", 3840, 2160);

    // Closest to 640x480 first: 704x480 (30,720 px away) before 640x360 (76,800) before 1280x720 (614,400).
    RTF_ASSERT(tokens(substream_candidates({main, prof("720p", "s", "H264", 1280, 720), prof("360p", "s", "H264", 640, 360),
                                            prof("d1", "s", "H264", 704, 480)}, main))
               == vector<string>({"d1", "360p", "720p"}));

    // Equally close (both 76,800 px away): 640 wide or more first.
    RTF_ASSERT(tokens(substream_candidates({main, prof("narrow", "s", "H264", 480, 480), prof("wide", "s", "H264", 640, 360)}, main))
               == vector<string>({"wide", "narrow"}));

    // Same size: H.264 before H.265, then Baseline or Main before High, then the higher frame rate.
    RTF_ASSERT(tokens(substream_candidates({main, prof("h265", "s", "H265", 640, 480, "", 30), prof("high", "s", "H264", 640, 480, "High", 30),
                                            prof("main15", "s", "H264", 640, 480, "Main", 15), prof("base25", "s", "H264", 640, 480, "Baseline", 25)}, main))
               == vector<string>({"base25", "main15", "high", "h265"}));

    // Nothing else to tell them apart: the camera's order.
    RTF_ASSERT(tokens(substream_candidates({main, prof("b", "s", "H264", 640, 480), prof("a", "s", "H264", 640, 480)}, main))
               == vector<string>({"b", "a"}));
}

void test_r_onvif_profiles::test_no_substream()
{
    auto main = prof("main", "s", "H264", 1920, 1080);
    RTF_ASSERT(substream_candidates({main}, main).empty());
    RTF_ASSERT(substream_candidates({}, main).empty());
    // A camera that names no video source anywhere: every profile counts as the same sensor.
    auto bare = prof("m", "", "H264", 1920, 1080);
    RTF_ASSERT(tokens(substream_candidates({bare, prof("s", "", "H264", 640, 360)}, bare)) == vector<string>({"s"}));
    // But one that names a source isn't matched to a main that names none.
    RTF_ASSERT(substream_candidates({bare, prof("s", "VideoSource_1", "H264", 640, 360)}, bare).empty());
}

// GetNodes: which continuous-move axes a camera drives, and "unknown" when it
// names no SupportedPTZSpaces (not "can't").
void test_r_onvif_profiles::test_ptz_caps()
{
    auto node = [](const string& spaces) {
        return string("<tptz:GetNodesResponse><tptz:PTZNode token=\"n1\"><tt:Name>PTZ</tt:Name>") +
               (spaces.empty() ? string() : "<tt:SupportedPTZSpaces>" + spaces + "</tt:SupportedPTZSpaces>") +
               "</tptz:PTZNode></tptz:GetNodesResponse>";
    };
    const string pt = "<tt:ContinuousPanTiltVelocitySpace><tt:URI>x</tt:URI></tt:ContinuousPanTiltVelocitySpace>";
    const string zoom = "<tt:ContinuousZoomVelocitySpace><tt:URI>x</tt:URI></tt:ContinuousZoomVelocitySpace>";

    auto both = parse_ptz_caps(node(pt + zoom));
    RTF_ASSERT(both.known && both.pan_tilt && both.zoom);

    auto pan_only = parse_ptz_caps(node(pt));
    RTF_ASSERT(pan_only.known && pan_only.pan_tilt && !pan_only.zoom);

    // Absolute and relative spaces only: no continuous axis to drive.
    auto other = parse_ptz_caps(node("<tt:AbsoluteZoomPositionSpace/><tt:RelativePanTiltTranslationSpace/>"));
    RTF_ASSERT(other.known && !other.pan_tilt && !other.zoom);

    // Two nodes: an axis counts if any node has it.
    auto two = parse_ptz_caps("<GetNodesResponse><PTZNode><SupportedPTZSpaces>" + pt + "</SupportedPTZSpaces></PTZNode>"
                              "<PTZNode><SupportedPTZSpaces>" + zoom + "</SupportedPTZSpaces></PTZNode></GetNodesResponse>");
    RTF_ASSERT(two.known && two.pan_tilt && two.zoom);

    // Other prefixes, matched by local name.
    auto prefixed = parse_ptz_caps("<a:GetNodesResponse xmlns:a=\"urn:a\"><a:PTZNode><a:SupportedPTZSpaces>"
                                   "<a:ContinuousZoomVelocitySpace/></a:SupportedPTZSpaces></a:PTZNode></a:GetNodesResponse>");
    RTF_ASSERT(prefixed.known && !prefixed.pan_tilt && prefixed.zoom);

    // A continuous space outside SupportedPTZSpaces doesn't count.
    auto outside = parse_ptz_caps("<GetNodesResponse><PTZNode><SupportedPTZSpaces/>" + zoom + "</PTZNode></GetNodesResponse>");
    RTF_ASSERT(outside.known && !outside.zoom);

    // No SupportedPTZSpaces at all: unknown, whatever else is there.
    auto unknown = parse_ptz_caps(node(""));
    RTF_ASSERT(!unknown.known);
    RTF_ASSERT(!parse_ptz_caps("<GetNodesResponse/>").known);

    RTF_ASSERT_THROWS(parse_ptz_caps("not xml <"), std::runtime_error);
}
