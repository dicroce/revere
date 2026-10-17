
#include "test_r_build_info.h"
#include "r_vss/r_build_info.h"
#include <regex>
#include <string>

using namespace r_vss;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_build_info);

// EXPECTED_VERSION / EXPECTED_BUILD_NUMBER / EXPECTED_GIT_REVISION are what the
// root CMakeLists computed when it configured (ut/CMakeLists.txt).

void test_r_build_info::test_version_and_build_number()
{
    // The configured version, plus "-dirty" when the tree had uncommitted changes at build time.
    auto v = revere_version();
    RTF_ASSERT(v == EXPECTED_VERSION || v == string(EXPECTED_VERSION) + "-dirty");
    RTF_ASSERT(revere_build_number() == EXPECTED_BUILD_NUMBER);
    RTF_ASSERT(revere_git_revision() == EXPECTED_GIT_REVISION);
}

void test_r_build_info::test_build_time_is_utc()
{
    RTF_ASSERT(regex_match(revere_build_time(), regex(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z)")));
}

void test_r_build_info::test_summary()
{
    auto s = revere_build_summary();
    RTF_ASSERT(s.find("Revere " + revere_version() + " (build " + to_string(revere_build_number()) + ")") == 0);
    RTF_ASSERT(s.find(revere_build_time()) != string::npos);
    RTF_ASSERT(s.find(revere_git_revision()) != string::npos);
}
