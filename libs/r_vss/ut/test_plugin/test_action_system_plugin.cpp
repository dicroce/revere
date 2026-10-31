// A system plugin for test_r_system_plugin_action: it appends each action it is
// given to <top_dir>/actions.txt. Built twice, the second time with
// NO_ACTION_EXPORT, as a plugin without system_plugin_action.
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

#ifndef NO_ACTION_EXPORT
R_API void system_plugin_action(r_system_plugin_handle h, const char* action)
{
    auto path = static_cast<test_plugin*>(h)->top_dir + "/actions.txt";
    if(auto f = fopen(path.c_str(), "a"))
    {
        fprintf(f, "%s\n", action);
        fclose(f);
    }
}
#endif

}
