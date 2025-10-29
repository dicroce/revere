
#ifndef __r_fakey_r_fake_camera_h
#define __r_fakey_r_fake_camera_h

#ifdef IS_WINDOWS
#pragma warning( push )
#pragma warning( disable : 4244 )
#endif
#ifdef IS_LINUX
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#ifdef IS_LINUX
#pragma GCC diagnostic pop
#endif
#ifdef IS_WINDOWS
#pragma warning( pop )
#endif
#include "r_av/r_demuxer.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include <string>
#include <vector>

namespace r_fakey
{

class r_fake_camera final
{
public:
    R_API r_fake_camera(
        const std::string& mount,
        const std::string& launch,
        int rtsp_port = 554,
        const std::string& username = std::string(),
        const std::string& password = std::string()
    );
    R_API r_fake_camera(
        const std::string& server_root,
        const std::vector<std::string>& file_names,
        int rtsp_port = 554,
        const std::string& username = std::string(),
        const std::string& password = std::string()
    );
    R_API r_fake_camera(const r_fake_camera&) = delete;
    R_API r_fake_camera(r_fake_camera&&) = delete;

    R_API ~r_fake_camera() noexcept;

    R_API r_fake_camera& operator=(const r_fake_camera&) = delete;
    R_API r_fake_camera& operator=(r_fake_camera&&) = delete;

    R_API void start() const;
    R_API void quit() const;

    R_API static std::string make_data_uri(const std::string& type, const uint8_t* p, size_t len);

private:
    void _common_init(int rtsp_port);
    GstRTSPMediaFactory* _prep_factory(const std::string& file_name, const std::string& username, const std::string& password);
    GstRTSPMediaFactory* _prep_factory(const std::string& mount, const std::string& launch, const std::string& username, const std::string& password);
    void _configure_auth(GstRTSPMediaFactory* factory, const std::string& username, const std::string& password);

    static std::pair<r_av::r_stream_info, r_av::r_stream_info> _get_stream_info(const std::string& file_name);

    std::string _server_root;
    GMainLoop* _loop;
    GstRTSPServer* _server;
    GstRTSPMountPoints* _mounts;
    std::vector<GstRTSPMediaFactory*> _factories;
};

}

#endif
