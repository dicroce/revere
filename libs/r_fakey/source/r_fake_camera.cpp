
#include "r_fakey/r_fake_camera.h"
#include "r_utils/r_file.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_exception.h"

#ifdef IS_WINDOWS
#include <Windows.h>
#endif
#ifdef IS_LINUX
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#endif

using namespace r_fakey;
using namespace r_utils;
using namespace r_av;
using namespace std;

r_fake_camera::r_fake_camera(
    const string& mount,
    const string& launch,
    int rtsp_port,
    const string& username,
    const string& password
) :
    _server_root(),
    _loop(g_main_loop_new(NULL, FALSE)),
    _server(gst_rtsp_onvif_server_new()),
    _mounts(nullptr),
    _factories()
{
    _common_init(rtsp_port);

    _factories.push_back(_prep_factory(mount, launch, username, password));

    gst_rtsp_server_attach(_server, NULL);
}

r_fake_camera::r_fake_camera(
    const string& server_root,
    const vector<string>& file_names,
    int rtsp_port,
    const string& username,
    const string& password
) :
    _server_root(server_root),
    _loop(g_main_loop_new(NULL, FALSE)),
    _server(gst_rtsp_onvif_server_new()),
    _mounts(nullptr),
    _factories()
{
    _common_init(rtsp_port);

    for(auto f : file_names)
        _factories.push_back(_prep_factory(f, username, password));

    gst_rtsp_server_attach(_server, NULL);
}

r_fake_camera::~r_fake_camera() noexcept
{
    for(auto f : _factories)
        g_object_unref(f);

    if(_mounts)
        g_object_unref(_mounts);

    if(_server)
        g_object_unref(_server);

    if(_loop)
        g_main_loop_unref(_loop);
}

void r_fake_camera::start() const
{
    g_main_loop_run(_loop);
}

void r_fake_camera::quit() const
{
    if(g_main_loop_is_running(_loop))
        g_main_loop_quit(_loop);
}

string r_fake_camera::make_data_uri(const string& type, const uint8_t* p, size_t len)
{
//    <img src="data:video/mp4;base64,iVBORw0KGgoAAA
//    ANSUhEUgAAAAUAAAAFCAYAAACNbyblAAAAHElEQVQI12P4
//    //8/w38GIAXDIBKE0DHxgljNBAAO9TXL0Y4OHwAAAABJRU
//    5ErkJggg==" />

    auto encoded = r_string_utils::to_base64(p, len);

    return r_string_utils::format("data:video/%s;base64,%s", type.c_str(), encoded.c_str());
}

void r_fake_camera::_common_init(int rtsp_port)
{
    //gst_rtsp_server_set_address(_server, INADDR_ANY);

    g_object_set(_server, "service", to_string(rtsp_port).c_str(), NULL);

    _mounts = gst_rtsp_server_get_mount_points(_server);
}

GstRTSPMediaFactory* r_fake_camera::_prep_factory(const string& file_name, const string& username, const string& password)
{
    auto si = _get_stream_info(_server_root + PATH_SLASH + file_name);

    auto media_file_name = (_server_root==".")?file_name:_server_root + PATH_SLASH + file_name;
    auto launch = "multifilesrc location=" + media_file_name;

    if(r_string_utils::contains(file_name, ".mp4"))
        launch += " ! qtdemux name=demux";
    else if(r_string_utils::contains(file_name, ".mkv"))
        launch += " ! matroskademux name=demux";

    if(si.second.codec_id == AV_CODEC_ID_AAC)
        launch += " ! aacparse ! rtpmp4apay pt=97 name=pay1 demux. ! queue";
    else if(si.second.codec_id == AV_CODEC_ID_PCM_MULAW)
        launch += " ! rtppcmupay pt=97 name=pay1 demux. ! queue";

    if(si.first.codec_id == AV_CODEC_ID_H264)
        launch += " ! h264parse ! rtph264pay config-interval=-1 pt=96 name=pay0";
    else if(si.first.codec_id == AV_CODEC_ID_H265)
        launch += " ! h265parse ! rtph265pay config-interval=-1 pt=96 name=pay0";
    else R_THROW(("Unsupported video codec"));

    GstRTSPMediaFactory* factory = gst_rtsp_media_factory_new();

    if(!factory)
        R_THROW(("Unable to create rtsp media factory."));

    gst_rtsp_media_factory_set_launch(factory, launch.c_str());
    gst_rtsp_mount_points_add_factory(_mounts, ("/" + file_name).c_str(), factory);

    if(!username.empty())
        _configure_auth(factory, username, password);

    return factory;
}

GstRTSPMediaFactory* r_fake_camera::_prep_factory(const string& mount, const string& launch, const string& username, const string& password)
{
    GstRTSPMediaFactory* factory = gst_rtsp_media_factory_new();

    gst_rtsp_media_factory_set_launch(factory, launch.c_str());
    gst_rtsp_mount_points_add_factory(_mounts, mount.c_str(), factory);

    if(!username.empty())
        _configure_auth(factory, username, password);

    return factory;
}

void r_fake_camera::_configure_auth(GstRTSPMediaFactory* factory, const string& username, const string& password)
{
    GstRTSPAuth* auth = gst_rtsp_auth_new();

    gst_rtsp_media_factory_add_role(
        factory, "anonymous",
        GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE,
        GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, FALSE, NULL
    );

    GstRTSPToken* default_token = gst_rtsp_token_new(
        GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING, "anonymous", NULL
    );

    gst_rtsp_auth_set_default_token(auth, default_token);
    gst_rtsp_token_unref(default_token);

    gst_rtsp_media_factory_add_role(
        factory, "user",
        GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE,
        GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, TRUE, NULL
    );

    GstRTSPToken* user_token = gst_rtsp_token_new(
        GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING, "user", NULL
    );

    gchar* basic = gst_rtsp_auth_make_basic(username.c_str(), password.c_str());
    gst_rtsp_auth_add_basic(auth, basic, user_token);
    g_free(basic);
    gst_rtsp_token_unref(user_token);

    // Finally attach our authenticator to our server...
    gst_rtsp_server_set_auth(_server, auth);
    g_object_unref(auth);
}

pair<r_stream_info, r_stream_info> r_fake_camera::_get_stream_info(const string& file_name)
{
    r_demuxer demux(file_name);
    auto vsi = demux.get_stream_info(demux.get_video_stream_index());
    auto asi = demux.get_stream_info(demux.get_audio_stream_index());
    return pair<r_stream_info, r_stream_info>(vsi, asi);
}
