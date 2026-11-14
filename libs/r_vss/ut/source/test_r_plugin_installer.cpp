#include "test_r_plugin_installer.h"
#include "plugin_manifest_fixture.h"
#include "r_vss/r_plugin_installer.h"
#include "r_utils/r_exception.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace r_vss;
using namespace r_utils;
using namespace std;
using namespace plugin_fixture;
namespace fs = std::filesystem;

REGISTER_TEST_FIXTURE(test_r_plugin_installer);

namespace
{

fs::path s_dir;

const string FILE_NAME = "test_system_plugin.dll";

r_plugin_release_assets _assets()
{
    r_plugin_release_assets a;
    a.plugin_url = "https://example.test/p.dll";
    a.manifest_url = "https://example.test/p.dll.json";
    a.signature_url = "https://example.test/p.dll.json.sig";
    return a;
}

// A fake GitHub: serves bodies by URL, 404s the rest, and logs what was fetched.
struct fake_server
{
    map<string, string> bodies;
    vector<string> fetched;
    bool offline {false};

    fake_server()
    {
        auto a = _assets();
        bodies[a.manifest_url] = MANIFEST;
        bodies[a.signature_url] = unhex(MANIFEST_SIG_HEX);
        bodies[a.plugin_url] = PLUGIN_BYTES;
    }

    r_plugin_fetch fetch()
    {
        return [this](const string& url, size_t max_bytes) {
            fetched.push_back(url);
            if(offline)
                R_STHROW(r_exception, ("no network"));
            auto it = bodies.find(url);
            if(it == bodies.end())
                R_STHROW(r_not_found_exception, ("Not found: %s", url.c_str()));
            if(it->second.size() > max_bytes)
                R_STHROW(r_exception, ("too big"));
            return vector<uint8_t>(it->second.begin(), it->second.end());
        };
    }
};

r_plugin_install_result _install(fake_server& server, const string& file_name = FILE_NAME,
                                 const vector<string>& keys = {TEST_KEY}, const string& version = "0.0.41")
{
    return install_system_plugin(_assets(), s_dir.string(), file_name, server.fetch(), version, keys, "windows", "x86_64");
}

string _read(const fs::path& p)
{
    ifstream f(p, ios::binary);
    stringstream s;
    s << f.rdbuf();
    return s.str();
}

void _write(const fs::path& p, const string& bytes)
{
    ofstream f(p, ios::binary | ios::trunc);
    f << bytes;
}

bool _throws_invalid(const string& url)
{
    try
    {
        parse_http_url(url);
    }
    catch(const r_invalid_argument_exception&)
    {
        return true;
    }
    return false;
}

size_t _files_in_dir()
{
    if(!fs::exists(s_dir))
        return 0;
    return (size_t)distance(fs::directory_iterator(s_dir), fs::directory_iterator());
}

}

void test_r_plugin_installer::setup()
{
    auto stamp = chrono::steady_clock::now().time_since_epoch().count();
    s_dir = fs::temp_directory_path() / ("r_plugin_installer_ut_" + to_string(stamp)) / "system_plugins";
}

void test_r_plugin_installer::teardown()
{
    error_code ec;
    fs::remove_all(s_dir.parent_path(), ec);
}

void test_r_plugin_installer::test_parse_url()
{
    auto u = parse_http_url("https://github.com/dicroce/revere/releases/download/v0.0.41/a.dll");
    RTF_ASSERT(u.scheme == "https");
    RTF_ASSERT(u.host == "github.com");
    RTF_ASSERT_EQUAL(u.port, 443);
    RTF_ASSERT(u.target == "/dicroce/revere/releases/download/v0.0.41/a.dll");

    // A signed CDN URL's query goes out exactly as given: not decoded, not reordered.
    auto q = "/x/y?X-Amz-Signature=ab%2Fcd&b=2&a=1&response-content-disposition=attachment%3B%20filename%3Da.dll";
    u = parse_http_url(string("https://objects.example.com") + q);
    RTF_ASSERT(u.target == q);

    u = parse_http_url("HTTP://localhost:8080");
    RTF_ASSERT(u.scheme == "http");
    RTF_ASSERT(u.host == "localhost");
    RTF_ASSERT_EQUAL(u.port, 8080);
    RTF_ASSERT(u.target == "/");

    u = parse_http_url("https://h.example?a=1#frag");
    RTF_ASSERT(u.target == "/?a=1");

    RTF_ASSERT(_throws_invalid("ftp://h.example/a"));
    RTF_ASSERT(_throws_invalid("github.com/a"));
    RTF_ASSERT(_throws_invalid("https:///a"));
    RTF_ASSERT(_throws_invalid("https://user:pw@h.example/a"));
    RTF_ASSERT(_throws_invalid("https://h.example:/a"));
    RTF_ASSERT(_throws_invalid("https://h.example:99999/a"));
    RTF_ASSERT(_throws_invalid("https://h.example:8a/a"));
    RTF_ASSERT(_throws_invalid("https://h.example/a b"));
    RTF_ASSERT(_throws_invalid("https://h.example/a\r\nX: y"));
}

void test_r_plugin_installer::test_resolve_redirect()
{
    auto from = "https://github.com/dicroce/revere/releases/download/v0.0.41/a.dll";
    RTF_ASSERT(resolve_redirect(from, "https://objects.example.com/x?s=1") == "https://objects.example.com/x?s=1");
    RTF_ASSERT(resolve_redirect(from, "//cdn.example.com/x") == "https://cdn.example.com/x");
    RTF_ASSERT(resolve_redirect(from, "/other") == "https://github.com/other");
    RTF_ASSERT(resolve_redirect("http://localhost:8080/a", "/b?c") == "http://localhost:8080/b?c");

    bool threw = false;
    try { resolve_redirect(from, "relative/path"); } catch(const r_invalid_argument_exception&) { threw = true; }
    RTF_ASSERT(threw);
    threw = false;
    try { resolve_redirect(from, "ftp://h.example/a"); } catch(const r_invalid_argument_exception&) { threw = true; }
    RTF_ASSERT(threw);
}

void test_r_plugin_installer::test_release_assets()
{
    auto base = string("https://github.com/dicroce/revere/releases/download/v0.0.41/");
    auto a = revere_cloud_release_assets("0.0.41", "windows", "x86_64");
    RTF_ASSERT(a.plugin_url == base + "revere-v0.0.41-windows-cloud-plugin.dll");
    RTF_ASSERT(a.manifest_url == base + "revere-v0.0.41-windows-cloud-plugin.dll.json");
    RTF_ASSERT(a.signature_url == base + "revere-v0.0.41-windows-cloud-plugin.dll.json.sig");

    RTF_ASSERT(revere_cloud_release_assets("0.0.41", "linux", "x86_64").plugin_url == base + "revere-v0.0.41-linux-cloud-plugin.so");
    RTF_ASSERT(revere_cloud_release_assets("0.0.41", "linux", "arm64").plugin_url == base + "revere-v0.0.41-linux-arm64-cloud-plugin.so");
    RTF_ASSERT(revere_cloud_release_assets("0.0.41", "macos", "arm64").plugin_url == base + "revere-v0.0.41-macos-cloud-plugin.dylib");
    RTF_ASSERT(revere_cloud_release_assets("0.0.41", "macos", "x86_64").plugin_url == base + "revere-v0.0.41-macos-intel-cloud-plugin.dylib");

    auto file = revere_cloud_plugin_file();
    RTF_ASSERT(file == "revere_cloud_system_plugin.dll" || file == "revere_cloud_system_plugin.dylib" || file == "revere_cloud_system_plugin.so");

    RTF_ASSERT(string(plugin_install_name(r_plugin_install::ok)) == "ok");
    RTF_ASSERT(string(plugin_install_name(r_plugin_install::not_available)) == "not_available");
    RTF_ASSERT(string(plugin_install_name(r_plugin_install::check_failed)) == "check_failed");
}

void test_r_plugin_installer::test_install()
{
    // Into a folder that doesn't exist yet: the manifest and signature first, then the plugin.
    fake_server server;
    auto r = _install(server);
    RTF_ASSERT(r.ok());
    RTF_ASSERT(r.detail.empty());
    RTF_ASSERT(fs::path(r.path) == s_dir / FILE_NAME);
    auto a = _assets();
    RTF_ASSERT_EQUAL(server.fetched.size(), (size_t)3);
    RTF_ASSERT(server.fetched[0] == a.manifest_url);
    RTF_ASSERT(server.fetched[1] == a.signature_url);
    RTF_ASSERT(server.fetched[2] == a.plugin_url);

    RTF_ASSERT(_read(s_dir / FILE_NAME) == PLUGIN_BYTES);
    RTF_ASSERT(_read(plugin_manifest_path((s_dir / FILE_NAME).string())) == MANIFEST);
    RTF_ASSERT(_read(plugin_signature_path((s_dir / FILE_NAME).string())) == unhex(MANIFEST_SIG_HEX));
    RTF_ASSERT_EQUAL(_files_in_dir(), (size_t)3);   // no temporary files left
    RTF_ASSERT(check_system_plugin(r.path, "0.0.41", {TEST_KEY}, "windows", "x86_64").ok());
}

void test_r_plugin_installer::test_install_replaces_an_old_copy()
{
    // An older install (another version's files) and a temporary file an interrupted one left.
    fs::create_directories(s_dir);
    _write(s_dir / FILE_NAME, "old plugin");
    _write(s_dir / (FILE_NAME + ".json"), "{\"old\": true}");
    _write(s_dir / (FILE_NAME + ".json.sig"), "old sig");
    _write(s_dir / (FILE_NAME + ".tmp-ABCDEFGH"), "left over");
    _write(s_dir / "other_plugin.dll", "not ours");
    // Uninstalled, but not yet restarted: installing again keeps it.
    _write(s_dir / (FILE_NAME + ".uninstall"), "uninstall\n");

    fake_server server;
    auto r = _install(server);
    RTF_ASSERT(r.ok());
    RTF_ASSERT(!fs::exists(s_dir / (FILE_NAME + ".uninstall")));
    RTF_ASSERT(_read(s_dir / FILE_NAME) == PLUGIN_BYTES);
    RTF_ASSERT(_read(s_dir / (FILE_NAME + ".json")) == MANIFEST);
    RTF_ASSERT(!fs::exists(s_dir / (FILE_NAME + ".tmp-ABCDEFGH")));
    RTF_ASSERT(_read(s_dir / "other_plugin.dll") == "not ours");
    RTF_ASSERT_EQUAL(_files_in_dir(), (size_t)4);
}

void test_r_plugin_installer::test_install_rejects()
{
    auto a = _assets();

    {
        // No build for this platform: the manifest 404s.
        fake_server server;
        server.bodies.erase(a.manifest_url);
        auto r = _install(server);
        RTF_ASSERT(r.status == r_plugin_install::not_available);
        RTF_ASSERT_EQUAL(server.fetched.size(), (size_t)1);
    }
    {
        // No network.
        fake_server server;
        server.offline = true;
        RTF_ASSERT(_install(server).status == r_plugin_install::download_failed);
    }
    {
        // The signature missing is a failed download, not "not available".
        fake_server server;
        server.bodies.erase(a.signature_url);
        RTF_ASSERT(_install(server).status == r_plugin_install::download_failed);
    }
    {
        // Signed by a key this revere doesn't trust: the plugin is never fetched.
        fake_server server;
        auto r = _install(server, FILE_NAME, {OTHER_KEY});
        RTF_ASSERT(r.status == r_plugin_install::check_failed);
        RTF_ASSERT(r.check == r_plugin_check::bad_signature);
        RTF_ASSERT_EQUAL(server.fetched.size(), (size_t)2);
    }
    {
        // A manifest altered after signing.
        fake_server server;
        server.bodies[a.manifest_url] = MANIFEST + " ";
        auto r = _install(server);
        RTF_ASSERT(r.check == r_plugin_check::bad_signature);
    }
    {
        // For another revere, or another file.
        fake_server server;
        RTF_ASSERT(_install(server, FILE_NAME, {TEST_KEY}, "0.0.42").check == r_plugin_check::wrong_version);
        RTF_ASSERT(_install(server, "other.dll").check == r_plugin_check::wrong_file);
    }
    {
        // The plugin swapped, or cut short.
        fake_server server;
        server.bodies[a.plugin_url] = "revere test plugiN\n";
        auto r = _install(server);
        RTF_ASSERT(r.status == r_plugin_install::check_failed);
        RTF_ASSERT(r.check == r_plugin_check::hash_mismatch);
        server.bodies[a.plugin_url] = "revere";
        RTF_ASSERT(_install(server).check == r_plugin_check::size_mismatch);
        // Bigger than the manifest says: the fetch itself refuses it.
        server.bodies[a.plugin_url] = PLUGIN_BYTES + "more";
        RTF_ASSERT(_install(server).status == r_plugin_install::download_failed);
    }

    // None of those wrote anything.
    RTF_ASSERT_EQUAL(_files_in_dir(), (size_t)0);

    {
        // A failed install leaves an old copy as it was.
        fs::create_directories(s_dir);
        _write(s_dir / FILE_NAME, "old plugin");
        fake_server server;
        server.bodies[a.plugin_url] = "revere test plugiN\n";
        RTF_ASSERT(_install(server).check == r_plugin_check::hash_mismatch);
        RTF_ASSERT(_read(s_dir / FILE_NAME) == "old plugin");
        RTF_ASSERT_EQUAL(_files_in_dir(), (size_t)1);
    }
}

// The release base a build installs from: the x.y.z it starts with, then nothing or "-".
void test_r_plugin_installer::test_release_base_version()
{
    RTF_ASSERT(release_base_version("0.0.42") == "0.0.42");
    RTF_ASSERT(release_base_version("0.0.42-dirty") == "0.0.42");
    RTF_ASSERT(release_base_version("0.0.41-5-gabc1234") == "0.0.41");
    RTF_ASSERT(release_base_version("0.0.41-5-gabc1234-dirty") == "0.0.41");
    RTF_ASSERT(release_base_version("10.20.300-1-g0") == "10.20.300");
    RTF_ASSERT(release_base_version("").empty());
    RTF_ASSERT(release_base_version("unknown").empty());
    RTF_ASSERT(release_base_version("abc1234").empty());
    RTF_ASSERT(release_base_version("abc1234-dirty").empty());
    RTF_ASSERT(release_base_version("v0.0.41").empty());
    RTF_ASSERT(release_base_version("0.0").empty());
    RTF_ASSERT(release_base_version("0.0-dirty").empty());
    RTF_ASSERT(release_base_version("0.0.").empty());
    RTF_ASSERT(release_base_version("0..41").empty());
    RTF_ASSERT(release_base_version("0.0.41.1").empty());
    RTF_ASSERT(release_base_version("0.0.41x").empty());
    RTF_ASSERT(release_base_version("0.0.41 ").empty());
    RTF_ASSERT(release_base_version("0.0.1234567890").empty());   // too many digits to compare
}

void test_r_plugin_installer::test_compare_release_versions()
{
    RTF_ASSERT(compare_release_versions("0.0.41", "0.0.41") == 0);
    RTF_ASSERT(compare_release_versions("0.0.40", "0.0.41") < 0);
    RTF_ASSERT(compare_release_versions("0.0.42", "0.0.41") > 0);
    RTF_ASSERT(compare_release_versions("0.0.9", "0.0.10") < 0);     // numbers, not text
    RTF_ASSERT(compare_release_versions("0.1.0", "0.0.99") > 0);
    RTF_ASSERT(compare_release_versions("1.0.0", "0.99.99") > 0);
    RTF_ASSERT(compare_release_versions("0.2.1", "0.10.0") < 0);
    for(auto bad : {"0.0.41-dirty", "", "unknown", "0.0"})
    {
        bool threw = false;
        try { compare_release_versions(bad, "0.0.41"); } catch(const r_invalid_argument_exception&) { threw = true; }
        RTF_ASSERT(threw);
        threw = false;
        try { compare_release_versions("0.0.41", bad); } catch(const r_invalid_argument_exception&) { threw = true; }
        RTF_ASSERT(threw);
    }
}

namespace
{

// GitHub's release JSON for one release, with the named assets.
string _release_json(const string& tag, const vector<string>& assets, bool draft = false, bool prerelease = false)
{
    string a;
    for(const auto& n : assets)
        a += string(a.empty() ? "" : ",") + "{\"name\":\"" + n + "\",\"size\":1}";
    return "{\"tag_name\":\"" + tag + "\",\"draft\":" + (draft ? "true" : "false") +
           ",\"prerelease\":" + (prerelease ? "true" : "false") + ",\"assets\":[" + a + "]}";
}

// A release's three Revere Cloud plugin assets for a platform.
vector<string> _plugin_assets(const string& version, const string& platform, const string& arch)
{
    auto a = revere_cloud_release_assets(version, platform, arch);
    vector<string> out;
    for(auto url : {a.plugin_url, a.manifest_url, a.signature_url})
        out.push_back(url.substr(url.rfind('/') + 1));
    return out;
}

vector<string> _with(vector<string> a, const vector<string>& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

// A fake GitHub with the fixture's plugin (windows, x86_64, for 0.0.41) at a release's URLs.
void _serve_release(fake_server& server, const string& version, const string& manifest = MANIFEST,
                    const string& signature = unhex(MANIFEST_SIG_HEX))
{
    auto a = revere_cloud_release_assets(version, "windows", "x86_64");
    server.bodies[a.manifest_url] = manifest;
    server.bodies[a.signature_url] = signature;
    server.bodies[a.plugin_url] = PLUGIN_BYTES;
}

r_plugin_install_result _install_cloud(fake_server& server, const string& revere_version)
{
    return install_revere_cloud_plugin(s_dir.string(), revere_version, FILE_NAME, server.fetch(), {TEST_KEY}, "windows", "x86_64");
}

}

void test_r_plugin_installer::test_parse_published_releases()
{
    auto json = "[" + _release_json("v0.0.42", {"a.dmg"}) + "," +
                _release_json("v0.0.43", {"x"}, true) + "," +          // a draft
                _release_json("v0.0.44", {"x"}, false, true) + "," +   // a prerelease
                _release_json("nightly", {"x"}) + "," +
                _release_json("v0.0.41-rc1", {"x"}) + "," +
                _release_json("0.0.40", {"x"}) + "," +                  // no "v"
                _release_json("v0.0.39", {}) + "," +
                "{\"name\":\"no tag\"}," +
                "\"not an object\"" + "]";
    auto r = parse_published_releases(json);
    RTF_ASSERT_EQUAL(r.size(), (size_t)2);
    RTF_ASSERT(r[0].version == "0.0.42");
    RTF_ASSERT(r[0].assets == vector<string>{"a.dmg"});
    RTF_ASSERT(r[1].version == "0.0.39");
    RTF_ASSERT(r[1].assets.empty());

    RTF_ASSERT(parse_published_releases("[]").empty());
    for(auto bad : {"", "{}", "not json", "{\"tag_name\":\"v0.0.41\"}"})
    {
        bool threw = false;
        try { parse_published_releases(bad); } catch(const r_invalid_argument_exception&) { threw = true; }
        RTF_ASSERT(threw);
    }
}

void test_r_plugin_installer::test_choose_plugin_release()
{
    // Out of order, as a list might be: 0.0.43 is newer than the build, 0.0.42 has no
    // signature yet (its release is still uploading), 0.0.40 has no Windows plugin.
    auto w = [](const string& v) { return _plugin_assets(v, "windows", "x86_64"); };
    vector<r_published_release> releases = {
        {"0.0.40", _plugin_assets("0.0.40", "linux", "x86_64")},
        {"0.0.43", w("0.0.43")},
        {"0.0.41", _with(w("0.0.41"), {"revere-v0.0.41-windows-setup.exe"})},
        {"0.0.42", {w("0.0.42")[0], w("0.0.42")[1]}},
        {"0.0.39", w("0.0.39")},
    };
    RTF_ASSERT(choose_plugin_release(releases, "0.0.42", "windows", "x86_64") == "0.0.41");
    RTF_ASSERT(choose_plugin_release(releases, "0.0.41", "windows", "x86_64") == "0.0.41");   // at the base
    RTF_ASSERT(choose_plugin_release(releases, "0.0.43", "windows", "x86_64") == "0.0.43");
    RTF_ASSERT(choose_plugin_release(releases, "0.0.99", "windows", "x86_64") == "0.0.43");
    RTF_ASSERT(choose_plugin_release(releases, "0.0.40", "windows", "x86_64") == "0.0.39");
    RTF_ASSERT(choose_plugin_release(releases, "0.0.38", "windows", "x86_64").empty());
    RTF_ASSERT(choose_plugin_release(releases, "0.0.40", "linux", "x86_64") == "0.0.40");
    RTF_ASSERT(choose_plugin_release(releases, "0.0.42", "linux", "x86_64") == "0.0.40");
    RTF_ASSERT(choose_plugin_release(releases, "0.0.42", "macos", "arm64").empty());
    RTF_ASSERT(choose_plugin_release({}, "0.0.42", "windows", "x86_64").empty());
    // Only a base version: a dev version's base is worked out first.
    RTF_ASSERT(choose_plugin_release(releases, "0.0.42-dirty", "windows", "x86_64").empty());
    RTF_ASSERT(choose_plugin_release(releases, "", "windows", "x86_64").empty());

    // A manifest and signature with no plugin beside them isn't one to install either.
    vector<r_published_release> no_plugin = {{"0.0.41", w("0.0.41")}, {"0.0.42", {w("0.0.42")[1], w("0.0.42")[2]}}};
    RTF_ASSERT(choose_plugin_release(no_plugin, "0.0.42", "windows", "x86_64") == "0.0.41");

    // The Intel Mac's assets are "macos-intel".
    vector<r_published_release> mac = {{"0.0.41", _plugin_assets("0.0.41", "macos", "x86_64")}};
    RTF_ASSERT(mac[0].assets[0] == "revere-v0.0.41-macos-intel-cloud-plugin.dylib");
    RTF_ASSERT(choose_plugin_release(mac, "0.0.42", "macos", "x86_64") == "0.0.41");
    RTF_ASSERT(choose_plugin_release(mac, "0.0.42", "macos", "arm64").empty());
}

// A release build is as before: its own release's assets, never the list of
// releases, and a manifest for exactly its version.
void test_r_plugin_installer::test_install_release_build_unchanged()
{
    {
        fake_server server;
        _serve_release(server, "0.0.41");
        auto r = _install_cloud(server, "0.0.41");
        RTF_ASSERT(r.ok());
        RTF_ASSERT(r.release_version == "0.0.41");
        auto a = revere_cloud_release_assets("0.0.41", "windows", "x86_64");
        RTF_ASSERT_EQUAL(server.fetched.size(), (size_t)3);
        RTF_ASSERT(server.fetched[0] == a.manifest_url);
        RTF_ASSERT(server.fetched[1] == a.signature_url);
        RTF_ASSERT(server.fetched[2] == a.plugin_url);
        RTF_ASSERT(check_system_plugin(r.path, "0.0.41", {TEST_KEY}, "windows", "x86_64").ok());
        teardown();
    }
    {
        // 0.0.42's release not out yet: not available, and 0.0.41's isn't taken instead.
        fake_server server;
        _serve_release(server, "0.0.41");
        server.bodies[REVERE_RELEASES_API_URL] = "[" + _release_json("v0.0.41", _plugin_assets("0.0.41", "windows", "x86_64")) + "]";
        auto r = _install_cloud(server, "0.0.42");
        RTF_ASSERT(r.status == r_plugin_install::not_available);
        RTF_ASSERT(find(server.fetched.begin(), server.fetched.end(), string(REVERE_RELEASES_API_URL)) == server.fetched.end());
        RTF_ASSERT_EQUAL(_files_in_dir(), (size_t)0);
    }
    {
        // A 0.0.41 manifest in 0.0.42's release: wrong_version, as before.
        fake_server server;
        _serve_release(server, "0.0.42");
        auto r = _install_cloud(server, "0.0.42");
        RTF_ASSERT(r.status == r_plugin_install::check_failed);
        RTF_ASSERT(r.check == r_plugin_check::wrong_version);
        RTF_ASSERT_EQUAL(_files_in_dir(), (size_t)0);
    }
}

// A dev build installs from the newest release at or below its base that has the
// plugin, and the manifest must be for exactly that release.
void test_r_plugin_installer::test_install_dev_build()
{
    for(auto version : {"0.0.42-dirty", "0.0.41-5-gabc1234", "0.0.41-dirty"})
    {
        fake_server server;
        _serve_release(server, "0.0.41");
        server.bodies[REVERE_RELEASES_API_URL] =
            "[" + _release_json("v0.0.43", _plugin_assets("0.0.43", "windows", "x86_64")) + "," +
            _release_json("v0.0.42", {"revere-v0.0.42-windows-setup.exe"}) + "," +
            _release_json("v0.0.41", _plugin_assets("0.0.41", "windows", "x86_64")) + "," +
            _release_json("v0.0.40", _plugin_assets("0.0.40", "windows", "x86_64")) + "]";
        auto r = _install_cloud(server, version);
        RTF_ASSERT(r.ok());
        RTF_ASSERT(r.release_version == "0.0.41");
        auto a = revere_cloud_release_assets("0.0.41", "windows", "x86_64");
        RTF_ASSERT_EQUAL(server.fetched.size(), (size_t)4);
        RTF_ASSERT(server.fetched[0] == REVERE_RELEASES_API_URL);
        RTF_ASSERT(server.fetched[1] == a.manifest_url);
        RTF_ASSERT(server.fetched[2] == a.signature_url);
        RTF_ASSERT(server.fetched[3] == a.plugin_url);
        RTF_ASSERT(_read(s_dir / FILE_NAME) == PLUGIN_BYTES);
        RTF_ASSERT(check_system_plugin(r.path, "0.0.41", {TEST_KEY}, "windows", "x86_64").ok());
        teardown();
    }
}

void test_r_plugin_installer::test_install_dev_build_rejects()
{
    auto list_041_as = [](const string& tag) {
        return "[" + _release_json(tag, _plugin_assets(tag.substr(1), "windows", "x86_64")) + "]";
    };
    {
        // The fixture's 0.0.41 manifest in 0.0.40's release: the manifest must be for
        // the release it came from, so wrong_version.
        fake_server server;
        _serve_release(server, "0.0.40");
        server.bodies[REVERE_RELEASES_API_URL] = list_041_as("v0.0.40");
        auto r = _install_cloud(server, "0.0.40-3-gabc1234");
        RTF_ASSERT(r.status == r_plugin_install::check_failed);
        RTF_ASSERT(r.check == r_plugin_check::wrong_version);
        RTF_ASSERT_EQUAL(_files_in_dir(), (size_t)0);
    }
    {
        // The signature is still checked.
        fake_server server;
        _serve_release(server, "0.0.41", MANIFEST, unhex(NOT_A_MANIFEST_SIG_HEX));
        server.bodies[REVERE_RELEASES_API_URL] = list_041_as("v0.0.41");
        auto r = _install_cloud(server, "0.0.42-dirty");
        RTF_ASSERT(r.status == r_plugin_install::check_failed);
        RTF_ASSERT(r.check == r_plugin_check::bad_signature);
        RTF_ASSERT_EQUAL(_files_in_dir(), (size_t)0);
    }
    {
        // And the hash.
        fake_server server;
        _serve_release(server, "0.0.41");
        server.bodies[revere_cloud_release_assets("0.0.41", "windows", "x86_64").plugin_url] = "revere test plugiN\n";
        server.bodies[REVERE_RELEASES_API_URL] = list_041_as("v0.0.41");
        auto r = _install_cloud(server, "0.0.42-dirty");
        RTF_ASSERT(r.status == r_plugin_install::check_failed);
        RTF_ASSERT(r.check == r_plugin_check::hash_mismatch);
        RTF_ASSERT_EQUAL(_files_in_dir(), (size_t)0);
    }
    {
        // No release at or below the base has one.
        fake_server server;
        _serve_release(server, "0.0.43");
        server.bodies[REVERE_RELEASES_API_URL] = list_041_as("v0.0.43");
        auto r = _install_cloud(server, "0.0.42-dirty");
        RTF_ASSERT(r.status == r_plugin_install::not_available);
        RTF_ASSERT_EQUAL(server.fetched.size(), (size_t)1);
    }
    {
        // The list can't be fetched, or isn't one.
        fake_server server;
        server.offline = true;
        auto r = _install_cloud(server, "0.0.42-dirty");
        RTF_ASSERT(r.status == r_plugin_install::download_failed);
        fake_server garbled;
        garbled.bodies[REVERE_RELEASES_API_URL] = "{\"message\":\"API rate limit exceeded\"}";
        r = _install_cloud(garbled, "0.0.42-dirty");
        RTF_ASSERT(r.status == r_plugin_install::download_failed);
        RTF_ASSERT_EQUAL(_files_in_dir(), (size_t)0);
    }
    {
        // A version with no base: nothing is fetched.
        for(auto version : {"unknown", "abc1234-dirty", ""})
        {
            fake_server server;
            auto r = _install_cloud(server, version);
            RTF_ASSERT(r.status == r_plugin_install::not_available);
            RTF_ASSERT(server.fetched.empty());
        }
    }
}

void test_r_plugin_installer::test_http_get_live()
{
    // Over the network, so only when asked: REVERE_TEST_HTTP_GET_URL names a URL (a
    // GitHub release asset follows its redirect to the CDN) and
    // REVERE_TEST_HTTP_GET_SIZE, if set, its size in bytes.
    const char* url = getenv("REVERE_TEST_HTTP_GET_URL");
    if(!url || !*url)
        return;
    auto body = http_get(url, 512 * 1024 * 1024);
    RTF_ASSERT(!body.empty());
    const char* size = getenv("REVERE_TEST_HTTP_GET_SIZE");
    if(size && *size)
        RTF_ASSERT_EQUAL(body.size(), (size_t)stoull(size));

    bool threw = false;
    try { http_get(url, 1); } catch(const r_exception&) { threw = true; }
    RTF_ASSERT(threw);

    // An asset that isn't there is a 404, which install_system_plugin reports as not_available.
    bool not_found = false;
    try { http_get(string(url) + ".no-such-asset", 1024); } catch(const r_not_found_exception&) { not_found = true; }
    RTF_ASSERT(not_found);
}
