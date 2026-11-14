
#include "utils.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_file.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/3rdparty/json/json.h"
#include "r_http/r_client_request.h"
#include "r_http/r_client_response.h"
#include "r_av/r_video_encoder.h"

#include "r_utils/r_local_api_token.h"

#include <deque>
#include <numeric>
#include <iterator>

using namespace std;
using namespace std::chrono;
using namespace r_utils;
using namespace r_http;
using namespace vision;
using json = nlohmann::json;

string vision::top_dir()
{
    // The folder Revere, Vision and revere_mcp share for this user.
    auto path = r_local_api_token::default_top_dir();
    if(path.empty())
        R_THROW(("Could not get the path to this user's data folder."));
    r_fs::mkdir_p(path);  // mkdir_p: another process may make it at the same moment
    return path;
}

string vision::sub_dir(const string& subdir)
{
    auto top = top_dir();
    auto sd = top + PATH_SLASH + subdir + PATH_SLASH;

    // mkdir_p, not mkdir: two processes starting at once (two View windows, say)
    // both see it missing, and the second mkdir would throw on the first's dir.
    r_fs::mkdir_p(sd);

    return sd;
}

string vision::join_path(const string& path, const string& fileName)
{
    return path + PATH_SLASH + fileName;
}

// Reduce a camera label to a filesystem-safe file stem.
static string _snapshot_safe_stem(const string& name)
{
    string out;
    for(char c : name)
    {
        if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
            out.push_back(c);
        else if(c == ' ')
            out.push_back('_');
    }
    if(out.empty())
        out = "camera";
    return out;
}

string vision::save_snapshot_png(const string& camera_label, const uint8_t* data, bool nv12, uint16_t w, uint16_t h, uint16_t original_h)
{
    if(!data || w == 0 || h == 0)
        R_THROW(("No frame to snapshot."));

    // Convert the display frame to packed RGB24 for the PNG encoder. swscale
    // is already linked (via r_av); it handles both NV12 and BGRA sources.
    auto src_fmt = nv12 ? AV_PIX_FMT_NV12 : AV_PIX_FMT_BGRA;
    SwsContext* sws = sws_getContext(w, h, src_fmt, w, h, AV_PIX_FMT_RGB24, SWS_POINT, nullptr, nullptr, nullptr);
    if(!sws)
        R_THROW(("Unable to create snapshot colour converter."));

    if(nv12)
    {
        // Match the GPU path: HD cameras are BT.709, SD ones BT.601.
        int* inv_table = nullptr; int* table = nullptr;
        int src_range = 0, dst_range = 0, brightness = 0, contrast = 0, saturation = 0;
        if(sws_getColorspaceDetails(sws, &inv_table, &src_range, &table, &dst_range, &brightness, &contrast, &saturation) >= 0)
        {
            const int* coeffs = sws_getCoefficients(original_h > 576 ? SWS_CS_ITU709 : SWS_CS_ITU601);
            sws_setColorspaceDetails(sws, coeffs, src_range, table, dst_range, brightness, contrast, saturation);
        }
    }

    const uint8_t* src_planes[4] = {nullptr, nullptr, nullptr, nullptr};
    int src_strides[4] = {0, 0, 0, 0};
    if(nv12)
    {
        src_planes[0] = data;
        src_planes[1] = data + ((size_t)w * h);
        src_strides[0] = w;
        src_strides[1] = w;
    }
    else
    {
        src_planes[0] = data;
        src_strides[0] = w * 4;
    }

    vector<uint8_t> rgb((size_t)w * h * 3);
    uint8_t* dst_planes[4] = {rgb.data(), nullptr, nullptr, nullptr};
    int dst_strides[4] = {w * 3, 0, 0, 0};

    int rows = sws_scale(sws, src_planes, src_strides, 0, h, dst_planes, dst_strides);
    sws_freeContext(sws);
    if(rows != h)
        R_THROW(("Snapshot colour conversion failed."));

    // ffmpeg's native PNG encoder; bitrate/gop/profile/level are meaningless for a still.
    r_av::r_video_encoder encoder(AV_CODEC_ID_PNG, 0, w, h, {1, 1}, AV_PIX_FMT_RGB24, 0, 1, 0, 0);
    encoder.attach_buffer(rgb.data(), rgb.size(), 0);
    auto es = encoder.encode();
    if(es != r_av::R_CODEC_STATE_HAS_OUTPUT)
        es = encoder.flush();
    if(es != r_av::R_CODEC_STATE_HAS_OUTPUT)
        R_THROW(("PNG encoder produced no output."));
    auto pi = encoder.get();

    auto t = system_clock::to_time_t(system_clock::now());
    auto bdt = r_utils::r_time_utils::to_local_tm(t);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d_%H-%M-%S", &bdt);

    // sub_dir() creates the folder and returns it with a trailing slash.
    auto path = sub_dir("snapshots") + _snapshot_safe_stem(camera_label) + "_" + ts + ".png";
    r_fs::write_file(pi.data, pi.size, path);
    return path;
}
