#include "test_r_plugin_manifest.h"
#include "plugin_manifest_fixture.h"
#include "r_vss/r_plugin_manifest.h"
#include "r_vss/r_system_plugin.h"
#include "r_utils/r_ecdsa.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_string_utils.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace r_vss;
using namespace r_utils;
using namespace std;
namespace fs = std::filesystem;

REGISTER_TEST_FIXTURE(test_r_plugin_manifest);

// The signed fixture (plugin_manifest_fixture.h).
using namespace plugin_fixture;

static string _unhex(const string& hex)
{
    return unhex(hex);
}

static bool _verify(const string& key, const string& data, const string& sig)
{
    return ecdsa_p256_sha256_verify(key, (const uint8_t*)data.data(), data.size(), (const uint8_t*)sig.data(), sig.size());
}

static void _write(const fs::path& p, const string& bytes)
{
    ofstream f(p, ios::binary | ios::trunc);
    f.write(bytes.data(), (streamsize)bytes.size());
}

static fs::path s_dir;

void test_r_plugin_manifest::setup()
{
    auto stamp = chrono::steady_clock::now().time_since_epoch().count();
    s_dir = fs::temp_directory_path() / ("r_plugin_manifest_ut_" + to_string(stamp));
    fs::create_directories(s_dir);
}

void test_r_plugin_manifest::teardown()
{
    error_code ec;
    fs::remove_all(s_dir, ec);
}

// The plugin, its manifest and the signature, as an installer leaves them; returns the plugin's path.
static string _install(const string& name = "test_system_plugin.dll",
                       const string& plugin = PLUGIN_BYTES,
                       const string& manifest = MANIFEST,
                       const string& sig_hex = MANIFEST_SIG_HEX)
{
    auto path = (s_dir / name).string();
    _write(path, plugin);
    _write(plugin_manifest_path(path), manifest);
    _write(plugin_signature_path(path), _unhex(sig_hex));
    return path;
}

static r_plugin_check _check(const string& path, const vector<string>& keys = {TEST_KEY},
                             const string& version = "0.0.41", const string& platform = "windows", const string& arch = "x86_64")
{
    return check_system_plugin(path, version, keys, platform, arch).status;
}

void test_r_plugin_manifest::test_ecdsa_verify()
{
    auto sig = _unhex(MANIFEST_SIG_HEX);
    RTF_ASSERT(_verify(TEST_KEY, MANIFEST, sig));
    RTF_ASSERT(_verify(TEST_KEY, NOT_A_MANIFEST, _unhex(NOT_A_MANIFEST_SIG_HEX)));

    // Another key, other data, or the signature of other data: no.
    RTF_ASSERT(!_verify(OTHER_KEY, MANIFEST, sig));
    RTF_ASSERT(!_verify(TEST_KEY, MANIFEST + " ", sig));
    RTF_ASSERT(!_verify(TEST_KEY, MANIFEST, _unhex(NOT_A_MANIFEST_SIG_HEX)));

    // A damaged signature: a flipped bit, cut short, a byte added, or none.
    auto flipped = sig;
    flipped[10] ^= 0x01;
    RTF_ASSERT(!_verify(TEST_KEY, MANIFEST, flipped));
    RTF_ASSERT(!_verify(TEST_KEY, MANIFEST, sig.substr(0, sig.size() - 1)));
    RTF_ASSERT(!_verify(TEST_KEY, MANIFEST, sig + "x"));
    RTF_ASSERT(!_verify(TEST_KEY, MANIFEST, ""));

    // A key that isn't one.
    RTF_ASSERT(!_verify("", MANIFEST, sig));
    RTF_ASSERT(!_verify("-----BEGIN PUBLIC KEY-----\nnot base64\n-----END PUBLIC KEY-----\n", MANIFEST, sig));
}

void test_r_plugin_manifest::test_parse()
{
    auto m = parse_plugin_manifest(MANIFEST);
    RTF_ASSERT(m.format == 1);
    RTF_ASSERT(m.platform == "windows");
    RTF_ASSERT(m.arch == "x86_64");
    RTF_ASSERT(m.revere_version == "0.0.41");
    RTF_ASSERT(m.file == "test_system_plugin.dll");
    RTF_ASSERT(m.size == 19);
    RTF_ASSERT(m.sha256 == "20616576e59d83e78286ed236fe12429c6830f0e851b8e69c882a0f637f5755e");

    // Fields it doesn't know are fine (a later manifest may add some).
    auto later = MANIFEST.substr(0, MANIFEST.size() - 1) + ",\"notes\":\"x\"}";
    RTF_ASSERT(parse_plugin_manifest(later).file == "test_system_plugin.dll");

    // "plugin_api" is optional: 0 when it isn't there.
    RTF_ASSERT(m.plugin_api == 0);
    RTF_ASSERT(parse_plugin_manifest(MANIFEST.substr(0, MANIFEST.size() - 1) + ",\"plugin_api\":\"1.2\"}").plugin_api == (1u << 16 | 2));
    RTF_ASSERT(parse_plugin_manifest(OTHER_API_MANIFEST).plugin_api == 65535u << 16);
}

void test_r_plugin_manifest::test_parse_rejects()
{
    auto with = [](const string& key, const string& value) {
        auto j = string("{\"format\":1,\"platform\":\"windows\",\"arch\":\"x86_64\",\"revere_version\":\"0.0.41\","
                        "\"file\":\"p.dll\",\"size\":19,\"sha256\":\"20616576e59d83e78286ed236fe12429c6830f0e851b8e69c882a0f637f5755e\"}");
        auto at = j.find("\"" + key + "\":");
        auto start = at + key.size() + 3;
        auto end = j.find_first_of(",}", start);
        if(key == "sha256" || key == "file" || key == "platform" || key == "arch" || key == "revere_version")
            end = j.find('"', start + 1) + 1;
        return j.substr(0, start) + value + j.substr(end);
    };
    auto rejects = [](const string& json) {
        try
        {
            parse_plugin_manifest(json);
        }
        catch(const r_invalid_argument_exception&)
        {
            return true;
        }
        return false;
    };

    RTF_ASSERT(!rejects(with("file", "\"p.dll\"")));   // the helper itself makes a good one

    RTF_ASSERT(rejects(""));
    RTF_ASSERT(rejects("not json"));
    RTF_ASSERT(rejects("[1,2]"));
    RTF_ASSERT(rejects("{}"));
    RTF_ASSERT(rejects(with("format", "2")));
    RTF_ASSERT(rejects(with("format", "\"1\"")));
    RTF_ASSERT(rejects(with("size", "-1")));
    RTF_ASSERT(rejects(with("size", "19.5")));
    RTF_ASSERT(rejects(with("size", "\"19\"")));
    RTF_ASSERT(rejects(with("platform", "1")));
    RTF_ASSERT(rejects(with("file", "\"\"")));
    RTF_ASSERT(rejects(with("file", "\"../p.dll\"")));
    RTF_ASSERT(rejects(with("file", "\"sub\\\\p.dll\"")));
    RTF_ASSERT(rejects(with("file", "\"C:p.dll\"")));
    RTF_ASSERT(rejects(with("file", "\"..\"")));
    RTF_ASSERT(rejects(with("sha256", "\"20616576E59D83E78286ED236FE12429C6830F0E851B8E69C882A0F637F5755E\"")));
    RTF_ASSERT(rejects(with("sha256", "\"2061\"")));
    RTF_ASSERT(rejects(with("sha256", "\"zz616576e59d83e78286ed236fe12429c6830f0e851b8e69c882a0f637f5755e\"")));

    // A "plugin_api" there must be "<major>.<minor>" (parse_plugin_api()).
    auto with_api = [&](const string& value) {
        auto j = with("file", "\"p.dll\"");
        return j.substr(0, j.size() - 1) + ",\"plugin_api\":" + value + "}";
    };
    RTF_ASSERT(!rejects(with_api("\"1.0\"")));
    RTF_ASSERT(!rejects(with_api("\"65535.65535\"")));
    RTF_ASSERT(rejects(with_api("1")));
    RTF_ASSERT(rejects(with_api("1.5")));
    RTF_ASSERT(rejects(with_api("\"1\"")));
    RTF_ASSERT(rejects(with_api("\"0.1\"")));
}

void test_r_plugin_manifest::test_check_ok()
{
    auto path = _install();
    auto r = check_system_plugin(path, "0.0.41", {TEST_KEY}, "windows", "x86_64");
    RTF_ASSERT(r.ok());
    RTF_ASSERT(r.detail.empty());

    // Any one trusted key is enough (a rotation trusts two).
    RTF_ASSERT(_check(path, {OTHER_KEY, TEST_KEY}) == r_plugin_check::ok);
}

void test_r_plugin_manifest::test_check_signature()
{
    auto path = _install();

    // No trusted keys, or only one that didn't sign it.
    RTF_ASSERT(_check(path, {}) == r_plugin_check::bad_signature);
    RTF_ASSERT(_check(path, {OTHER_KEY}) == r_plugin_check::bad_signature);
    RTF_ASSERT(_check(path, {"not a key"}) == r_plugin_check::bad_signature);

    // The manifest changed after signing, even by a space.
    _write(plugin_manifest_path(path), MANIFEST + " ");
    RTF_ASSERT(_check(path) == r_plugin_check::bad_signature);

    // A signature that's empty, or far too big to be one.
    path = _install();
    _write(plugin_signature_path(path), "");
    RTF_ASSERT(_check(path) == r_plugin_check::bad_signature);
    _write(plugin_signature_path(path), string(4096, 'x'));
    RTF_ASSERT(_check(path) == r_plugin_check::bad_signature);

    // No signature, or no manifest at all (an older installer's plugin).
    path = _install();
    fs::remove(plugin_signature_path(path));
    RTF_ASSERT(_check(path) == r_plugin_check::bad_signature);
    fs::remove(plugin_manifest_path(path));
    RTF_ASSERT(_check(path) == r_plugin_check::no_manifest);
    RTF_ASSERT(_check(path, {}) == r_plugin_check::no_manifest);

    // A manifest too big to be one is never read.
    path = _install();
    _write(plugin_manifest_path(path), string(65 * 1024, ' '));
    RTF_ASSERT(_check(path) == r_plugin_check::unreadable);
}

void test_r_plugin_manifest::test_check_contents()
{
    // Signed, but not a manifest.
    auto path = _install("test_system_plugin.dll", PLUGIN_BYTES, NOT_A_MANIFEST, NOT_A_MANIFEST_SIG_HEX);
    RTF_ASSERT(_check(path) == r_plugin_check::malformed);

    // The manifest names another file: renaming a plugin doesn't make it this one.
    path = _install("renamed_plugin.dll");
    RTF_ASSERT(_check(path) == r_plugin_check::wrong_file);

    // Another platform or architecture.
    path = _install();
    RTF_ASSERT(_check(path, {TEST_KEY}, "0.0.41", "linux") == r_plugin_check::wrong_platform);
    RTF_ASSERT(_check(path, {TEST_KEY}, "0.0.41", "windows", "arm64") == r_plugin_check::wrong_platform);

    // Another revere version, even a near one: exact match only.
    RTF_ASSERT(_check(path, {TEST_KEY}, "0.0.42") == r_plugin_check::wrong_version);
    RTF_ASSERT(_check(path, {TEST_KEY}, "0.0.41-3-gabc1234") == r_plugin_check::wrong_version);
    RTF_ASSERT(_check(path, {TEST_KEY}, "0.0.41-dirty") == r_plugin_check::wrong_version);
    RTF_ASSERT(_check(path, {TEST_KEY}, "") == r_plugin_check::wrong_version);

    // A different plugin file: longer, or the same size with other bytes.
    path = _install("test_system_plugin.dll", PLUGIN_BYTES + "x");
    RTF_ASSERT(_check(path) == r_plugin_check::size_mismatch);
    auto swapped = PLUGIN_BYTES;
    swapped[0] = 'R';
    path = _install("test_system_plugin.dll", swapped);
    RTF_ASSERT(_check(path) == r_plugin_check::hash_mismatch);

    // For another plugin API: refused, as for another revere version, and after it.
    path = _install("test_system_plugin.dll", PLUGIN_BYTES, OTHER_API_MANIFEST, OTHER_API_MANIFEST_SIG_HEX);
    RTF_ASSERT(_check(path, {API_TEST_KEY}) == r_plugin_check::wrong_plugin_api);
    RTF_ASSERT(check_system_plugin(path, "0.0.41", {API_TEST_KEY}, "windows", "x86_64").detail ==
               "it needs plugin interface v65535, this revere has v" + to_string(R_SYSTEM_PLUGIN_API_MAJOR));
    RTF_ASSERT(_check(path, {API_TEST_KEY}, "0.0.42") == r_plugin_check::wrong_version);
    r_plugin_manifest m;
    RTF_ASSERT(check_plugin_manifest(OTHER_API_MANIFEST, _unhex(OTHER_API_MANIFEST_SIG_HEX), "test_system_plugin.dll", "0.0.41",
                                     {API_TEST_KEY}, "windows", "x86_64", m).status == r_plugin_check::wrong_plugin_api);

    // The plugin itself gone, with its manifest and signature still there.
    path = _install();
    fs::remove(path);
    RTF_ASSERT(_check(path) == r_plugin_check::unreadable);
}

void test_r_plugin_manifest::test_names_and_paths()
{
    RTF_ASSERT(plugin_manifest_path("/a/b/p.dll") == "/a/b/p.dll.json");
    RTF_ASSERT(plugin_signature_path("/a/b/p.dll") == "/a/b/p.dll.json.sig");

    RTF_ASSERT(string(plugin_check_name(r_plugin_check::ok)) == "ok");
    RTF_ASSERT(string(plugin_check_name(r_plugin_check::no_manifest)) == "no_manifest");
    RTF_ASSERT(string(plugin_check_name(r_plugin_check::wrong_version)) == "wrong_version");
    RTF_ASSERT(string(plugin_check_name(r_plugin_check::hash_mismatch)) == "hash_mismatch");

    auto platform = plugin_platform();
    RTF_ASSERT(platform == "windows" || platform == "macos" || platform == "linux");
    auto arch = plugin_arch();
    RTF_ASSERT(arch == "x86_64" || arch == "arm64");

    // The release key, and only it: a signature Scott made with its private half
    // over this text (openssl dgst -sha256 -sign, 2026-11-12) verifies with it.
    RTF_ASSERT_EQUAL(trusted_plugin_keys().size(), (size_t)1);
    const string text = "revere plugin key check 2026-11-12";
    auto sig = r_string_utils::from_base64("MEUCIQCClUP76LUnhLxIDYCAhHB7hGOweRKTGFSkKa6sS28HpwIgAfUYspJFs/I5XyRyHnt5BaiMgpuyVFqLB6M5rOWqxp0=");
    RTF_ASSERT(ecdsa_p256_sha256_verify(trusted_plugin_keys()[0], reinterpret_cast<const uint8_t*>(text.data()), text.size(), sig.data(), sig.size()));
    RTF_ASSERT(!ecdsa_p256_sha256_verify(trusted_plugin_keys()[0], reinterpret_cast<const uint8_t*>(text.data()), text.size() - 1, sig.data(), sig.size()));
}

void test_r_plugin_manifest::test_plugin_api_versions()
{
    auto v = [](uint32_t major, uint32_t minor) { return major << 16 | minor; };
    RTF_ASSERT(R_SYSTEM_PLUGIN_API_VERSION == v(R_SYSTEM_PLUGIN_API_MAJOR, R_SYSTEM_PLUGIN_API_MINOR));
    RTF_ASSERT(R_SYSTEM_PLUGIN_API_MAJOR >= 1);

    // Names, both ways.
    RTF_ASSERT(plugin_api_name(v(1, 0)) == "1.0");
    RTF_ASSERT(plugin_api_name(v(2, 13)) == "2.13");
    uint32_t got = 0;
    RTF_ASSERT(parse_plugin_api("1.0", got) && got == v(1, 0));
    RTF_ASSERT(parse_plugin_api("2.13", got) && got == v(2, 13));
    RTF_ASSERT(parse_plugin_api("65535.65535", got) && got == v(65535, 65535));
    for(auto bad : {"", "1", "1.", ".1", "0.1", "1.0.0", "1.-1", "-1.0", "+1.0", " 1.0", "1.0 ", "65536.0", "1.65536",
                    "a.b", "1,0", "0000001.0"})
        RTF_ASSERT(!parse_plugin_api(bad, got));

    // The same major, and a minor no later than the host's: loads.
    RTF_ASSERT(check_plugin_api_version(v(1, 0), v(1, 0)).ok());
    RTF_ASSERT(check_plugin_api_version(v(1, 0), v(1, 3)).ok());
    RTF_ASSERT(check_plugin_api_version(v(1, 3), v(1, 3)).ok());

    // A later minor needs what this host doesn't have yet.
    auto r = check_plugin_api_version(v(1, 4), v(1, 3));
    RTF_ASSERT(r.status == r_plugin_check::wrong_plugin_api);
    RTF_ASSERT(r.detail == "it needs plugin interface v1.4, this revere has v1.3");

    // Another major, older or newer, whatever the minors.
    r = check_plugin_api_version(v(2, 0), v(1, 3));
    RTF_ASSERT(r.status == r_plugin_check::wrong_plugin_api);
    RTF_ASSERT(r.detail == "it needs plugin interface v2, this revere has v1");
    r = check_plugin_api_version(v(1, 0), v(2, 5));
    RTF_ASSERT(r.status == r_plugin_check::wrong_plugin_api);
    RTF_ASSERT(r.detail == "it needs plugin interface v1, this revere has v2");
    RTF_ASSERT(plugin_check_name(r_plugin_check::wrong_plugin_api) == string("wrong_plugin_api"));
}

void test_r_plugin_manifest::test_check_plugin_api()
{
    auto v = [](uint32_t major, uint32_t minor) { return major << 16 | minor; };
    const uint32_t host = v(3, 2);
    auto plugin = (s_dir / "p.dylib").string();
    _write(plugin, PLUGIN_BYTES);
    auto manifest = [&](const string& json) { _write(plugin_manifest_path(plugin), json); };

    // Nothing to go on: ok, and the plugin's own export decides once it's open.
    RTF_ASSERT(check_plugin_api(plugin, host).ok());
    for(auto json : {string("not json"), string("[1]"), string("{}"), MANIFEST, string("{\"plugin_api\":2}"),
                     string("{\"plugin_api\":\"two\"}"), string("{\"plugin_api\":\"9\"}")})
    {
        manifest(json);
        RTF_ASSERT(check_plugin_api(plugin, host).ok());
    }

    // One this host loads: ok, signed or not.
    manifest("{\"plugin_api\":\"3.2\"}");
    RTF_ASSERT(check_plugin_api(plugin, host).ok());
    manifest("{\"plugin_api\":\"3.0\"}");
    RTF_ASSERT(check_plugin_api(plugin, host).ok());

    // One it doesn't: refused, signature or not (this only ever refuses).
    manifest("{\"plugin_api\":\"3.3\"}");
    auto r = check_plugin_api(plugin, host);
    RTF_ASSERT(r.status == r_plugin_check::wrong_plugin_api);
    RTF_ASSERT(r.detail == "it needs plugin interface v3.3, this revere has v3.2");
    manifest("{\"plugin_api\":\"2.9\"}");
    RTF_ASSERT(check_plugin_api(plugin, host).detail == "it needs plugin interface v2, this revere has v3");
    manifest(OTHER_API_MANIFEST);
    RTF_ASSERT(check_plugin_api(plugin, host).status == r_plugin_check::wrong_plugin_api);
}

void test_r_plugin_manifest::test_load_error_text()
{
    // macOS dyld: the missing library's file name, from its load command.
    RTF_ASSERT(plugin_load_error_text(
        "Failed to load library '/Users/x/Library/Application Support/Revere/revere/system_plugins/revere_cloud_system_plugin.dylib': "
        "dlopen(/Users/x/Library/Application Support/Revere/revere/system_plugins/revere_cloud_system_plugin.dylib, 0x0002): "
        "Library not loaded: @executable_path/../libs/libavcodec.62.28.103.dylib\n"
        "  Referenced from: <4C4C4447-5555-3144-A1B2-C3D4E5F60718> /Users/x/revere_cloud_system_plugin.dylib\n"
        "  Reason: tried: '/Users/x/revere.app/Contents/libs/libavcodec.62.28.103.dylib' (no such file)") ==
        "it needs libavcodec.62.28.103.dylib, which this revere doesn't have");
    RTF_ASSERT(plugin_load_error_text("dlopen(p.dylib, 0x0002): Library not loaded: @rpath/libr_new.dylib") ==
               "it needs libr_new.dylib, which this revere doesn't have");

    // Linux ld.so.
    RTF_ASSERT(plugin_load_error_text(
        "Failed to load library '/home/x/revere/system_plugins/revere_cloud_system_plugin.so': "
        "libavcodec.so.62: cannot open shared object file: No such file or directory") ==
        "it needs libavcodec.so.62, which this revere doesn't have");

    // Windows LoadLibrary.
    RTF_ASSERT(plugin_load_error_text("Failed to load library 'C:\\p.dll'. Windows error code: 126") ==
               "it needs a library this revere doesn't have");
    RTF_ASSERT(plugin_load_error_text("Failed to load library 'C:\\p.dll'. Windows error code: 127") ==
               "it needs a function this revere's libraries don't have");

    // Anything else: the error as it is.
    RTF_ASSERT(plugin_load_error_text("dlopen(p.dylib, 0x0002): no suitable image found") == "dlopen(p.dylib, 0x0002): no suitable image found");
    RTF_ASSERT(plugin_load_error_text("") == "");
    RTF_ASSERT(plugin_load_error_text("Library not loaded: ") == "Library not loaded: ");
}
