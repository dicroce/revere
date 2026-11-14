// A system plugin for test_r_system_plugin_action: it appends each action it is
// given to <top_dir>/actions.txt, and each start and stop to lifecycle.txt.
// Built three times:
//   - action: with system_plugin_action, for this revere's plugin API;
//   - no_action: with NO_ACTION_EXPORT and NO_API_EXPORT, as a plugin from before
//     system_plugin_action and get_system_plugin_api_version;
//   - other_api: with TEST_PLUGIN_API_VERSION another plugin API, and RECORD_LOAD,
//     so load_system_plugin writes <top_dir>/loaded.txt if it's ever called.
#include "r_vss/r_system_plugin.h"
#include <cstdio>
#include <string>

namespace
{
struct test_plugin
{
    std::string top_dir;
    bool enabled {true};
};

// Appends line to <top_dir>/name, for the tests to read.
void append(r_system_plugin_handle h, const char* name, const char* line)
{
    auto path = static_cast<test_plugin*>(h)->top_dir + "/" + name;
    if(auto f = fopen(path.c_str(), "a"))
    {
        fprintf(f, "%s\n", line);
        fclose(f);
    }
}
}

extern "C"
{

R_API const char* get_system_plugin_guid()
{
    return TEST_PLUGIN_GUID;
}

#ifndef NO_API_EXPORT
#ifndef TEST_PLUGIN_API_VERSION
#define TEST_PLUGIN_API_VERSION R_SYSTEM_PLUGIN_API_VERSION
#endif
R_API uint32_t get_system_plugin_api_version()
{
    return TEST_PLUGIN_API_VERSION;
}
#endif

R_API r_system_plugin_handle load_system_plugin(const char* top_dir, void*)
{
    auto p = new test_plugin {top_dir ? top_dir : "."};
#ifdef RECORD_LOAD
    append(p, "loaded.txt", "load");
#endif
    return p;
}

R_API void start_system_plugin(r_system_plugin_handle h)
{
    append(h, "lifecycle.txt", "start");
}

R_API void stop_system_plugin(r_system_plugin_handle h)
{
    append(h, "lifecycle.txt", "stop");
}

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

#ifndef NO_ACTION_EXPORT
R_API void system_plugin_action(r_system_plugin_handle h, const char* action)
{
    append(h, "actions.txt", action);
}
#endif

}
