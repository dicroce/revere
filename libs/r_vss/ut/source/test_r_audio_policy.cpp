#include "test_r_audio_policy.h"
#include "r_vss/r_audio_policy.h"
#include "r_vss/r_recording_context.h"
#include "r_utils/r_file.h"

using namespace r_vss;
using namespace r_utils;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_audio_policy);

static string _dir()
{
    return string("top_dir") + PATH_SLASH + "audio_policy_ut";
}

// What test_file leaves, also when an assert stopped it part way.
void test_r_audio_policy::teardown()
{
    auto config = _dir() + PATH_SLASH + "config";
    auto file = config + PATH_SLASH + "audio.json";
    if(r_fs::file_exists(file))
        r_fs::remove_file(file);
    for(auto& d : {config, _dir()})
        if(r_fs::file_exists(d))
            r_fs::rmdir(d);
}

// Only the whole shape: a malformed request changes nothing.
void test_r_audio_policy::test_parse()
{
    RTF_ASSERT(parse_audio_policy("").is_null());
    RTF_ASSERT(parse_audio_policy("not json").is_null());
    RTF_ASSERT(parse_audio_policy("{}").is_null());
    RTF_ASSERT(parse_audio_policy("{\"default_audio\": \"no\"}").is_null());
    RTF_ASSERT(parse_audio_policy("{\"default_audio\": true, \"cameras\": []}").is_null());
    RTF_ASSERT(parse_audio_policy("{\"default_audio\": true, \"cameras\": {\"porch\": 0}}").is_null());
    RTF_ASSERT(parse_audio_policy("{\"default_audio\": true, \"cameras\": {\"\": false}}").is_null());

    auto p = parse_audio_policy("{\"default_audio\": false, \"cameras\": {\"porch\": true, \"yard\": false}}");
    RTF_ASSERT(!p.is_null());
    RTF_ASSERT(!p.value().default_audio);
    RTF_ASSERT((p.value().cameras == map<string, bool>{{"porch", true}, {"yard", false}}));
    // No cameras yet: the default only.
    auto none = parse_audio_policy("{\"default_audio\": true}");
    RTF_ASSERT(!none.is_null() && none.value().default_audio && none.value().cameras.empty());
    // Round trip.
    RTF_ASSERT(parse_audio_policy(audio_policy_json(p.value())).value() == p.value());

    // Who manages it: absent is this computer; "cloud" survives a round trip; anything else is refused.
    RTF_ASSERT(!p.value().cloud_managed);
    auto cloud = parse_audio_policy("{\"default_audio\": true, \"managed_by\": \"cloud\"}");
    RTF_ASSERT(!cloud.is_null() && cloud.value().cloud_managed);
    RTF_ASSERT(parse_audio_policy(audio_policy_json(cloud.value())).value().cloud_managed);
    RTF_ASSERT(!parse_audio_policy("{\"default_audio\": true, \"managed_by\": \"local\"}").value().cloud_managed);
    RTF_ASSERT(parse_audio_policy("{\"default_audio\": true, \"managed_by\": \"someone\"}").is_null());
    RTF_ASSERT(parse_audio_policy("{\"default_audio\": true, \"managed_by\": true}").is_null());
}

void test_r_audio_policy::test_records_audio()
{
    // Never told: every camera's audio, as before.
    r_audio_policy never;
    RTF_ASSERT(never.records_audio("porch"));

    r_audio_policy p;
    p.default_audio = false;
    p.cameras = {{"porch", true}, {"yard", false}};
    RTF_ASSERT(p.records_audio("porch"));
    RTF_ASSERT(!p.records_audio("yard"));
    // A camera the cloud hasn't heard of yet gets the default: off where the org or site is off.
    RTF_ASSERT(!p.records_audio("new"));
    p.default_audio = true;
    RTF_ASSERT(p.records_audio("new"));
    RTF_ASSERT(!p.records_audio("yard"));
}

// Kept across restarts; none saved, or a broken file, records audio.
void test_r_audio_policy::test_file()
{
    auto dir = _dir();
    r_fs::mkdir_p(dir);
    RTF_ASSERT(load_audio_policy(dir) == r_audio_policy());
    r_audio_policy p;
    p.default_audio = false;
    p.cameras = {{"porch", false}};
    RTF_ASSERT(save_audio_policy(dir, p));
    RTF_ASSERT(load_audio_policy(dir) == p);
    RTF_ASSERT(!load_audio_policy(dir).records_audio("porch"));

    auto path = dir + PATH_SLASH + "config" + PATH_SLASH + "audio.json";
    string junk = "{\"default_audio\": 0}";
    r_fs::atomic_write_file((const uint8_t*)junk.data(), junk.size(), path);
    RTF_ASSERT(load_audio_policy(dir) == r_audio_policy());
}

// Only the sessions whose audio no longer matches start again.
void test_r_audio_policy::test_restarts()
{
    r_audio_policy p;
    p.default_audio = true;
    p.cameras = {{"porch", false}, {"yard", true}};
    map<string, bool> running = {{"porch", true}, {"yard", true}, {"gate", true}, {"shed", false}};
    RTF_ASSERT((audio_policy_restarts(running, p) == set<string>{"porch", "shed"}));
    running = {{"porch", false}, {"yard", true}, {"gate", true}};
    RTF_ASSERT(audio_policy_restarts(running, p).empty());
    p.default_audio = false;
    RTF_ASSERT((audio_policy_restarts(running, p) == set<string>{"gate"}));
    RTF_ASSERT(audio_policy_restarts({}, p).empty());
}

// A session that doesn't record audio leaves the camera's audio out of what it offers: no audio track to store, to
// restream or to hand to plugins.
void test_r_audio_policy::test_sdp_without_audio()
{
    map<string, r_pipeline::r_sdp_media> medias;
    medias["video"] = r_pipeline::r_sdp_media();
    medias["audio"] = r_pipeline::r_sdp_media();
    auto kept = recording_sdp_medias(medias, true);
    RTF_ASSERT(kept.size() == 2 && kept.count("audio") == 1);
    auto without = recording_sdp_medias(medias, false);
    RTF_ASSERT(without.size() == 1 && without.count("video") == 1 && without.count("audio") == 0);
    RTF_ASSERT(recording_sdp_medias({}, false).empty());

    // The restream's audio: the camera's encoding while recording audio, none otherwise.
    r_nullable<r_pipeline::r_encoding> aac;
    aac.set_value(r_pipeline::AAC_GENERIC_ENCODING);
    RTF_ASSERT(!recording_audio_encoding(aac, true).is_null());
    RTF_ASSERT(recording_audio_encoding(aac, true).value() == r_pipeline::AAC_GENERIC_ENCODING);
    RTF_ASSERT(recording_audio_encoding(aac, false).is_null());
    RTF_ASSERT(recording_audio_encoding(r_nullable<r_pipeline::r_encoding>(), true).is_null());
}

// With no cloud, a person signed in sets the policy, as before; once the cloud plugin has, only it may.
void test_r_audio_policy::test_who_may_set_it()
{
    // No cloud: never told, or set by a person here.
    r_audio_policy none;
    RTF_ASSERT(audio_policy_set_allowed(none, false));
    RTF_ASSERT(audio_policy_set_allowed(none, true));
    r_audio_policy off;
    off.default_audio = false;
    auto by_person = audio_policy_as_set(off, false);
    RTF_ASSERT(!by_person.cloud_managed && !by_person.default_audio);
    RTF_ASSERT(audio_policy_set_allowed(by_person, false));   // and changed again

    // The plugin sets it: cloud-managed from then on, and a person can't change it, on or off.
    auto by_cloud = audio_policy_as_set(off, true);
    RTF_ASSERT(by_cloud.cloud_managed);
    RTF_ASSERT(!audio_policy_set_allowed(by_cloud, false));
    RTF_ASSERT(audio_policy_set_allowed(by_cloud, true));

    // What a request says about who manages it doesn't count: a person can't post "managed_by: cloud" to lock it,
    // nor the plugin "local" to free it.
    r_audio_policy claims_cloud;
    claims_cloud.cloud_managed = true;
    RTF_ASSERT(!audio_policy_as_set(claims_cloud, false).cloud_managed);
    RTF_ASSERT(audio_policy_as_set(none, true).cloud_managed);

    // Kept across restarts.
    auto dir = _dir();
    r_fs::mkdir_p(dir);
    RTF_ASSERT(save_audio_policy(dir, by_cloud));
    RTF_ASSERT(!audio_policy_set_allowed(load_audio_policy(dir), false));
    RTF_ASSERT(save_audio_policy(dir, by_person));
    RTF_ASSERT(audio_policy_set_allowed(load_audio_policy(dir), false));
}

// The cloud lets go: when the plugin signs out or unpairs, and at a start with no cloud plugin running. The values stay
// as they were; a person signed in may change them again.
void test_r_audio_policy::test_released()
{
    r_audio_policy off;
    off.default_audio = false;
    off.cameras = {{"porch", false}, {"yard", true}};
    auto by_cloud = audio_policy_as_set(off, true);
    RTF_ASSERT(!audio_policy_set_allowed(by_cloud, false));

    // The plugin signs out or unpairs.
    auto released = audio_policy_released(by_cloud);
    RTF_ASSERT(!released.cloud_managed);
    RTF_ASSERT(!released.default_audio && released.cameras == off.cameras);
    RTF_ASSERT(!released.records_audio("porch") && released.records_audio("yard") && !released.records_audio("new"));
    RTF_ASSERT(audio_policy_set_allowed(released, false));
    // Nothing for a running session to restart for: the same audio.
    RTF_ASSERT(audio_policy_restarts({{"porch", false}, {"yard", true}}, released).empty());

    // A start: still managed while the cloud plugin runs; released, same values, without it.
    RTF_ASSERT(audio_policy_at_startup(by_cloud, true) == by_cloud);
    auto started = audio_policy_at_startup(by_cloud, false);
    RTF_ASSERT(!started.cloud_managed && started.cameras == off.cameras && !started.default_audio);
    RTF_ASSERT(audio_policy_set_allowed(started, false));
    // A local policy stays as it is either way.
    auto local = audio_policy_as_set(off, false);
    RTF_ASSERT(audio_policy_at_startup(local, true) == local && audio_policy_at_startup(local, false) == local);

    // Which plugin is the cloud's.
    RTF_ASSERT(is_cloud_plugin_name("revere_cloud_system_plugin"));
    RTF_ASSERT(is_cloud_plugin_name("revere_cloud"));
    RTF_ASSERT(!is_cloud_plugin_name("ollama_system_plugin"));
    RTF_ASSERT(!is_cloud_plugin_name("my_revere_cloud"));
    RTF_ASSERT(!is_cloud_plugin_name(""));
}
