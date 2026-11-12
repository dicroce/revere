#include "test_r_plugin_manifest.h"
#include "r_vss/r_plugin_manifest.h"
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

// A fixture made with OpenSSL, the way the release CI signs:
//   openssl ecparam -name prime256v1 -genkey -noout -out key.pem
//   openssl ec -in key.pem -pubout -out pub.pem
//   openssl dgst -sha256 -sign key.pem -out manifest.json.sig manifest.json
// Only the public keys and the signatures are here; the private keys were thrown away.

static const string TEST_KEY =
    "-----BEGIN PUBLIC KEY-----\n"
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE6I2Lwyx1+Q0pr6GIMBhOCuJTwR/9\n"
    "Zg3mkyYoWDi4oFtEMELLeZUGHHf4+yvdBf//K0hT4TQ00P9oi/LOAQYM0w==\n"
    "-----END PUBLIC KEY-----\n";

// Another P-256 key, which signed nothing here.
static const string OTHER_KEY =
    "-----BEGIN PUBLIC KEY-----\n"
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEajuYUQhNvyK680+i7tAnNHVDpt6g\n"
    "uYbQXzLmQtgybfNuwuNItRb9bmmFoOIyhxathmD6Ab74gyVX8hPstXIiuw==\n"
    "-----END PUBLIC KEY-----\n";

// The plugin file the manifest describes (19 bytes).
static const string PLUGIN_BYTES = "revere test plugin\n";

static const string MANIFEST =
    "{\"format\":1,\"platform\":\"windows\",\"arch\":\"x86_64\",\"revere_version\":\"0.0.41\","
    "\"file\":\"test_system_plugin.dll\",\"size\":19,"
    "\"sha256\":\"20616576e59d83e78286ed236fe12429c6830f0e851b8e69c882a0f637f5755e\"}";

static const string MANIFEST_SIG_HEX =
    "304502201fc91886f8ed07cf168ea99996f6f291ccba5a1aeeb493e21d56d98a27e5dad3"
    "022100d5d82cfc03b0a6977cb28135fe3445ec7e981a1904fe6424edc270d3ad344361";

// Signed by TEST_KEY too, but not a manifest.
static const string NOT_A_MANIFEST = "not a manifest";
static const string NOT_A_MANIFEST_SIG_HEX =
    "3046022100d9b1c6cd98ad414254590fdfab44680d53c66eedd261beecd9141f916ebf28b1"
    "022100a049fbcb445b226a795da8f83748866fa6347a64528853ebd992fa04c67ff4a5";

static string _unhex(const string& hex)
{
    string out;
    for(size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back((char)stoi(hex.substr(i, 2), nullptr, 16));
    return out;
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
