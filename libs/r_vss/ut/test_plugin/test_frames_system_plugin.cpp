// A system plugin for test_r_system_plugin_frames: it reads frame feeds through the
// host's functions (system_plugin_set_host_api) when its actions say so, and
// appends what it got to <top_dir>/frames.txt.
//   sub:<camera>:<stream>:<media>[:<max bytes>]   subscribe (stream "-" for null)
//   subs:<camera>:<n>               n more video feeds, and how many it got
//   read:<n>:<full|small>           up to n frames; "small" passes an r_frame without its stream field
//   next                            one next_frame without waiting, and its result
//   unsub                           unsubscribe
#include "r_vss/r_system_plugin.h"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
struct test_plugin
{
    std::string top_dir;
    bool enabled {true};
    const r_system_plugin_host_api* api {nullptr};
    r_frame_subscription_handle sub {nullptr};
};

void note(test_plugin* p, const std::string& line)
{
    auto path = p->top_dir + "/frames.txt";
    if(auto f = fopen(path.c_str(), "a"))
    {
        fprintf(f, "%s\n", line.c_str());
        fclose(f);
    }
}

std::string field(const std::string& s, int n)
{
    size_t start = 0;
    for(int i = 0; i < n; ++i)
    {
        start = s.find(':', start);
        if(start == std::string::npos)
            return std::string();
        ++start;
    }
    auto end = s.find(':', start);
    return s.substr(start, end == std::string::npos ? std::string::npos : end - start);
}
}

extern "C"
{

R_API const char* get_system_plugin_guid()
{
    return "com.revere.test.frames";
}

R_API r_system_plugin_handle load_system_plugin(const char* top_dir, void*)
{
    return new test_plugin {top_dir ? top_dir : "."};
}

R_API void start_system_plugin(r_system_plugin_handle) {}
R_API void stop_system_plugin(r_system_plugin_handle) {}

R_API void destroy_system_plugin(r_system_plugin_handle h)
{
    delete static_cast<test_plugin*>(h);
}

R_API bool system_plugin_enabled(r_system_plugin_handle h)
{
    return static_cast<test_plugin*>(h)->enabled;
}

R_API void system_plugin_set_enabled(r_system_plugin_handle h, bool enabled)
{
    static_cast<test_plugin*>(h)->enabled = enabled;
}

R_API void system_plugin_set_host_api(r_system_plugin_handle h, const r_system_plugin_host_api* api)
{
    auto p = static_cast<test_plugin*>(h);
    p->api = api;
    note(p, "api version=" + std::to_string(api ? api->version : 0) + " size_ok=" +
            std::to_string(api && api->struct_size >= sizeof(r_system_plugin_host_api) ? 1 : 0));
}

R_API void system_plugin_action(r_system_plugin_handle h, const char* action)
{
    auto p = static_cast<test_plugin*>(h);
    std::string a = action ? action : "";
    if(!p->api)
    {
        note(p, "no api");
        return;
    }
    auto cmd = field(a, 0);
    if(cmd == "sub")
    {
        auto camera = field(a, 1), stream = field(a, 2);
        auto media = (uint32_t)strtoul(field(a, 3).c_str(), nullptr, 10);
        auto max = (size_t)strtoull(field(a, 4).c_str(), nullptr, 10);
        p->sub = p->api->subscribe_frames(p->api->host, camera.c_str(), stream == "-" ? nullptr : stream.c_str(), media, max);
        note(p, p->sub ? "sub ok" : "sub null");
    }
    else if(cmd == "subs")
    {
        auto camera = field(a, 1);
        int n = atoi(field(a, 2).c_str()), got = 0;
        for(int i = 0; i < n; ++i)
            got += p->api->subscribe_frames(p->api->host, camera.c_str(), nullptr, R_FRAME_VIDEO, 0) ? 1 : 0;
        note(p, "subs got=" + std::to_string(got));
    }
    else if(cmd == "read")
    {
        int n = atoi(field(a, 1).c_str());
        bool small = field(a, 2) == "small";
        for(int i = 0; i < n; ++i)
        {
            // A buffer bigger than r_frame, filled with a marker: a "small" r_frame
            // ends before its stream field, and nothing past it may be written.
            unsigned char buf[sizeof(r_frame) + 16];
            memset(buf, 0xA5, sizeof(buf));
            auto fr = reinterpret_cast<r_frame*>(buf);
            fr->struct_size = small ? (uint32_t)offsetof(r_frame, stream) : (uint32_t)sizeof(r_frame);
            int r = p->api->next_frame(p->api->host, p->sub, 200, fr);
            if(r != 1)
            {
                note(p, "next=" + std::to_string(r));
                break;
            }
            bool untouched = true;
            for(size_t b = small ? offsetof(r_frame, stream) : sizeof(r_frame); b < sizeof(buf); ++b)
                untouched = untouched && buf[b] == 0xA5;
            std::string line = "frame flags=" + std::to_string(fr->flags) + " ts=" + std::to_string(fr->ts) + " size=" + std::to_string(fr->size) +
                               " codec=" + std::string(fr->codec_name) + " params=" + std::string(fr->codec_params) +
                               " data0=" + std::to_string(fr->size ? fr->data[0] : 0);
            line += small ? std::string(" stream=") + (untouched ? "unwritten" : "WRITTEN") : std::string(" stream=") + fr->stream + (untouched ? "" : " OVERRUN");
            note(p, line);
        }
    }
    else if(cmd == "next")
    {
        r_frame fr;
        memset(&fr, 0, sizeof(fr));
        fr.struct_size = sizeof(fr);
        note(p, "next=" + std::to_string(p->api->next_frame(p->api->host, p->sub, 0, &fr)));
    }
    else if(cmd == "unsub")
    {
        p->api->unsubscribe(p->api->host, p->sub);
        p->sub = nullptr;
        note(p, "unsub");
    }
}

}
