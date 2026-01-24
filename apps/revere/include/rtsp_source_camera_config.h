#ifndef __revere_rtsp_source_camera_config_h
#define __revere_rtsp_source_camera_config_h

#include <string>

namespace revere
{

struct rtsp_source_camera_config
{
    std::string camera_name;
    std::string ipv4;
    std::string rtsp_url;
    std::string rtsp_username;
    std::string rtsp_password;
};

}

#endif
