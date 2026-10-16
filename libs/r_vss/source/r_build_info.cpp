
#include "r_vss/r_build_info.h"

#ifndef REVERE_VERSION_STRING
#define REVERE_VERSION_STRING "unknown"
#endif
#ifndef REVERE_BUILD_NUMBER
#define REVERE_BUILD_NUMBER 0
#endif
#ifndef REVERE_GIT_REVISION
#define REVERE_GIT_REVISION "unknown"
#endif

using namespace std;

// Defined in the revere_build_info.cpp generated on every build (build_info.cmake).
namespace r_vss { namespace build_stamp {
extern const char* const time_utc;
extern const bool dirty;
} }

string r_vss::revere_version()
{
    string v = REVERE_VERSION_STRING;
    return build_stamp::dirty ? v + "-dirty" : v;
}

int64_t r_vss::revere_build_number()
{
    return (int64_t)REVERE_BUILD_NUMBER;
}

string r_vss::revere_build_time()
{
    return build_stamp::time_utc;
}

string r_vss::revere_git_revision()
{
    return REVERE_GIT_REVISION;
}

string r_vss::revere_build_summary()
{
    return "Revere " + revere_version() + " (build " + to_string(revere_build_number()) + ")" +
           " \xC2\xB7 built " + revere_build_time() + " \xC2\xB7 " + revere_git_revision();
}
