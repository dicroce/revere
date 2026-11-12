#include "test_r_plugin_installer.h"
#include "plugin_manifest_fixture.h"
#include "r_vss/r_plugin_installer.h"
#include "r_utils/r_exception.h"
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

    fake_server server;
    auto r = _install(server);
    RTF_ASSERT(r.ok());
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
