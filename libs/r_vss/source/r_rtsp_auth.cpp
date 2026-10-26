#include "r_vss/r_rtsp_auth.h"
#include "r_vss/r_ws_auth.h"
#include "r_utils/r_file.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_logger.h"

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

using namespace r_vss;
using namespace r_utils;
using namespace std;

string r_rtsp_auth::load_or_create_password(const string& top_dir)
{
    auto path = top_dir + PATH_SLASH + "rtsp_password";
    try
    {
        if(r_fs::file_exists(path))
        {
            auto bytes = r_fs::read_file(path);
            auto pw = r_string_utils::strip(string(bytes.begin(), bytes.end()));
            if(pw.size() >= 32)
                return pw;
            R_LOG_WARNING("r_rtsp_auth: %s is unusable; generating a new RTSP password.", path.c_str());
        }
    }
    catch(const exception& ex)
    {
        R_LOG_WARNING("r_rtsp_auth: can't read %s (%s); generating a new RTSP password.", path.c_str(), ex.what());
    }

    auto pw = r_ws_auth::generate_token();
    r_fs::atomic_write_private_file((const uint8_t*)pw.data(), pw.size(), path);
    R_LOG_INFO("r_rtsp_auth: generated a new RTSP password in %s", path.c_str());
    return pw;
}

GstRTSPAuth* r_rtsp_auth::make_auth(const string& password)
{
    auto auth = gst_rtsp_auth_new();
    gst_rtsp_auth_set_supported_methods(auth, GST_RTSP_AUTH_DIGEST);

    auto token = gst_rtsp_token_new(GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING, VIEWER_ROLE, NULL);
    gst_rtsp_auth_add_digest(auth, RTSP_USER, password.c_str(), token);
    gst_rtsp_token_unref(token);

    return auth;
}

void r_rtsp_auth::allow_viewer(GstRTSPMediaFactory* factory)
{
    gst_rtsp_media_factory_add_role(factory, VIEWER_ROLE,
                                    GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE,
                                    GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, TRUE,
                                    NULL);
}
