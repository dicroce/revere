
#include "test_r_disco.h"
#include "r_disco/r_agent.h"
#include "r_disco/r_devices.h"
#include "r_utils/r_file.h"

using namespace std;
using namespace r_utils;

REGISTER_TEST_FIXTURE(test_r_disco);

using namespace r_disco;

void test_r_disco::setup()
{
    r_fs::mkdir("top_dir");
}

void test_r_disco::teardown()
{
    r_fs::rmdir("top_dir");
}

void test_r_disco::test_r_disco_r_agent_basics()
{
    r_agent agent("top_dir");
    vector<pair<r_stream_config, string>> saved_stream_configs;

    agent.set_credential_cb([](const string& id){
        printf("credential_cb: %s\n", id.c_str());
        return make_pair(r_nullable<string>("root"), r_nullable<string>("emperor1"));
    });
    agent.set_stream_change_cb([&](const vector<pair<r_stream_config, string>>& stream_configs){
        saved_stream_configs = stream_configs;
    });
    agent.set_is_recording_cb([](const string& id){
        printf("is_recording_cb: %s\n", id.c_str());
        return false;
    });
    agent.start();
    this_thread::sleep_for(chrono::milliseconds(20000));
    agent.stop();

    for (const auto& stream_config : saved_stream_configs) {
        printf("stream_config: %s\n", stream_config.first.camera_name.value().c_str());
        printf("stream_config: %s\n", stream_config.first.ipv4.value().c_str());
        printf("stream_config: %s\n", stream_config.first.xaddrs.value().c_str());
        printf("stream_config: %s\n", stream_config.first.address.value().c_str());
        printf("stream_config: %s\n", stream_config.first.rtsp_url.value().c_str());
    }
}
