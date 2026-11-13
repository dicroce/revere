// A system plugin for test_r_system_plugin_motion: it appends each system_plugin_post_motion call to
// <top_dir>/motion.txt ("camera started ts") and each system_plugin_camera_state call to <top_dir>/state.txt
// ("camera receiving failed"). A call for a camera named "slow..." takes half a second. Built twice, the second time with
// NO_MOTION_EXPORTS: a plugin with neither export (and the detection export a plugin of this kind has).
#include "r_vss/r_system_plugin.h"
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

namespace
{
struct test_plugin
{
    std::string top_dir;
    bool enabled {true};
};

void append(r_system_plugin_handle h, const char* name, const std::string& line)
{
    auto path = static_cast<test_plugin*>(h)->top_dir + "/" + name;
    if(auto f = fopen(path.c_str(), "a"))
    {
        fprintf(f, "%s\n", line.c_str());
        fclose(f);
    }
}

void maybe_slow(const char* camera_id)
{
    if(std::string(camera_id).rfind("slow", 0) == 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
}
}

extern "C"
{

R_API const char* get_system_plugin_guid()
{
    return TEST_PLUGIN_GUID;
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

R_API void system_plugin_post_detection(r_system_plugin_handle h, const char* camera_id, int64_t ts, const char* class_name,
                                        float, float, float, float, float)
{
    append(h, "detection.txt", std::string(camera_id) + " " + std::to_string(ts) + " " + class_name);
}

#ifndef NO_MOTION_EXPORTS
R_API void system_plugin_post_motion(r_system_plugin_handle h, const char* camera_id, int started, int64_t ts_ms)
{
    maybe_slow(camera_id);
    append(h, "motion.txt", std::string(camera_id) + " " + std::to_string(started) + " " + std::to_string(ts_ms));
}

R_API void system_plugin_camera_state(r_system_plugin_handle h, const char* camera_id, int receiving_video, int stream_failed)
{
    maybe_slow(camera_id);
    append(h, "state.txt", std::string(camera_id) + " " + std::to_string(receiving_video) + " " + std::to_string(stream_failed));
}
#endif

}
