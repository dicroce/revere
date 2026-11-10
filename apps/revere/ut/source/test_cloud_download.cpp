#include "test_cloud_download.h"
#include "cloud_download.h"
#include <string>

using namespace revere;
using namespace std;

REGISTER_TEST_FIXTURE(test_cloud_download);

void test_cloud_download::test_each_platform_gets_its_installer()
{
    // The asset names the release workflows publish. An Intel Mac's is its own: the Apple silicon one refuses to run there.
    const string base = "https://github.com/dicroce/revere/releases/download/v0.0.39/revere-v0.0.39";
    RTF_ASSERT(revere_cloud_download_url("0.0.39", cloud_platform::windows) == base + "-windows-cloud-setup.exe");
    RTF_ASSERT(revere_cloud_download_url("0.0.39", cloud_platform::linux_x86_64) == base + "-linux-cloud.run");
    RTF_ASSERT(revere_cloud_download_url("0.0.39", cloud_platform::linux_arm64) == base + "-linux-arm64-cloud.run");
    RTF_ASSERT(revere_cloud_download_url("0.0.39", cloud_platform::macos_arm64) == base + "-macos-cloud.command");
    RTF_ASSERT(revere_cloud_download_url("0.0.39", cloud_platform::macos_x86_64) == base + "-macos-intel-cloud.command");
}

void test_cloud_download::test_other_versions_open_the_latest_release()
{
    // A build that isn't a release has no installer of its own.
    for(auto v : {"", "unknown", "9639d77", "0.0.30-4-gabc1234", "0.0", "0.0.39.1", "0..39", "v0.0.39", "0.0.39 "})
        for(auto p : {cloud_platform::windows, cloud_platform::macos_x86_64})
            RTF_ASSERT(revere_cloud_download_url(v, p) == "https://github.com/dicroce/revere/releases/latest");
}

void test_cloud_download::test_this_build_platform()
{
    // The platform the test itself was built for.
#if defined(IS_WINDOWS)
    RTF_ASSERT(this_cloud_platform() == cloud_platform::windows);
#elif defined(IS_LINUX) && defined(__aarch64__)
    RTF_ASSERT(this_cloud_platform() == cloud_platform::linux_arm64);
#elif defined(IS_LINUX)
    RTF_ASSERT(this_cloud_platform() == cloud_platform::linux_x86_64);
#elif defined(IS_MACOS) && defined(__x86_64__)
    RTF_ASSERT(this_cloud_platform() == cloud_platform::macos_x86_64);
#else
    RTF_ASSERT(this_cloud_platform() == cloud_platform::macos_arm64);
#endif
}
