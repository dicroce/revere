#include "test_r_plugin_catalog.h"
#include "plugin_catalog_fixture.h"
#include "plugin_manifest_fixture.h"
#include "r_vss/r_plugin_catalog.h"
#include "r_utils/r_exception.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace r_vss;
using namespace r_utils;
using namespace std;
using namespace catalog_fixture;
namespace fs = std::filesystem;

REGISTER_TEST_FIXTURE(test_r_plugin_catalog);

namespace
{

fs::path s_dir;

const string BASE = "https://github.com/dicroce/revere/releases/download/v0.0.41/";
const string CATALOG_URL = BASE + "revere-v0.0.41-windows-plugins.json";

vector<string> _keys() { return {CATALOG_KEY, plugin_fixture::TEST_KEY}; }

string _sig(const string& hex) { return plugin_fixture::unhex(hex); }

r_plugin_check_result _check(const string& json, const string& sig_hex, const vector<string>& keys = {CATALOG_KEY},
                             const string& version = "0.0.41", const string& platform = "windows")
{
    r_plugin_catalog c;
    return check_plugin_catalog(json, _sig(sig_hex), version, keys, platform, "x86_64", c);
}

bool _parse_throws(const string& json)
{
    try
    {
        parse_plugin_catalog(json);
    }
    catch(const r_invalid_argument_exception&)
    {
        return true;
    }
    return false;
}

string _replace(string s, const string& from, const string& to)
{
    auto at = s.find(from);
    RTF_ASSERT(at != string::npos);
    return s.replace(at, from.size(), to);
}

// A fake GitHub: serves bodies by URL, 404s the rest, and logs what was fetched.
struct fake_server
{
    map<string, string> bodies;
    vector<string> fetched;
    bool offline {false};

    fake_server()
    {
        bodies[CATALOG_URL] = CATALOG;
        bodies[CATALOG_URL + ".sig"] = _sig(CATALOG_SIG_HEX);
        // Revere Cloud's plugin is the installer test's; "second" has its own manifest.
        bodies[BASE + "revere-v0.0.41-windows-cloud-plugin.dll"] = plugin_fixture::PLUGIN_BYTES;
        bodies[BASE + "revere-v0.0.41-windows-cloud-plugin.dll.json"] = plugin_fixture::MANIFEST;
        bodies[BASE + "revere-v0.0.41-windows-cloud-plugin.dll.json.sig"] = _sig(plugin_fixture::MANIFEST_SIG_HEX);
        bodies[BASE + "revere-v0.0.41-windows-second-plugin.dll"] = SECOND_BYTES;
        bodies[BASE + "revere-v0.0.41-windows-second-plugin.dll.json"] = SECOND_MANIFEST;
        bodies[BASE + "revere-v0.0.41-windows-second-plugin.dll.json.sig"] = _sig(SECOND_MANIFEST_SIG_HEX);
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

r_catalog_fetch_result _fetch(fake_server& server, const string& version = "0.0.41", const string& platform = "windows")
{
    return fetch_plugin_catalog(version, server.fetch(), _keys(), platform, "x86_64");
}

r_plugin_catalog _catalog()
{
    return parse_plugin_catalog(CATALOG);
}

string _read(const fs::path& p)
{
    ifstream f(p, ios::binary);
    stringstream s;
    s << f.rdbuf();
    return s.str();
}

r_skipped_system_plugin _skipped(const string& name, r_plugin_check status)
{
    r_skipped_system_plugin s;
    s.name = name;
    s.check.status = status;
    s.check.detail = "why";
    return s;
}

}

void test_r_plugin_catalog::setup()
{
    auto stamp = chrono::steady_clock::now().time_since_epoch().count();
    s_dir = fs::temp_directory_path() / ("r_plugin_catalog_ut_" + to_string(stamp)) / "system_plugins";
}

void test_r_plugin_catalog::teardown()
{
    error_code ec;
    fs::remove_all(s_dir.parent_path(), ec);
}

void test_r_plugin_catalog::test_parse()
{
    auto c = _catalog();
    RTF_ASSERT_EQUAL(c.format, 1);
    RTF_ASSERT(c.platform == "windows");
    RTF_ASSERT(c.arch == "x86_64");
    RTF_ASSERT(c.revere_version == "0.0.41");
    RTF_ASSERT_EQUAL(c.plugins.size(), 3u);

    auto cloud = c.find("revere_cloud");
    RTF_ASSERT(cloud);
    RTF_ASSERT(cloud->type == r_catalog_entry_type::plugin);
    RTF_ASSERT(cloud->name == "Revere Cloud");
    RTF_ASSERT(cloud->source == r_catalog_source::closed);
    RTF_ASSERT(!cloud->beta);
    RTF_ASSERT_EQUAL(cloud->terms_version, 2u);
    RTF_ASSERT(cloud->terms_url == "https://example.test/terms");
    RTF_ASSERT(cloud->file == "test_system_plugin.dll");
    RTF_ASSERT(cloud->plugin == "revere-v0.0.41-windows-cloud-plugin.dll");

    auto second = c.find("second");
    RTF_ASSERT(second);
    RTF_ASSERT(second->source == r_catalog_source::open);
    RTF_ASSERT(second->beta);
    RTF_ASSERT_EQUAL(second->terms_version, 0u);

    auto link = c.find("homekit");
    RTF_ASSERT(link);
    RTF_ASSERT(link->type == r_catalog_entry_type::link);
    RTF_ASSERT(link->url == "https://example.test/homekit");
    RTF_ASSERT(link->plugin.empty());

    RTF_ASSERT(!c.find("nothing"));
}

void test_r_plugin_catalog::test_parse_generated()
{
    // What scripts/release/write_plugin_catalog.py wrote for windows x86_64, with Revere Cloud and
    // Apple Home both in the release (the release CI's catalog, unsigned).
    auto c = parse_plugin_catalog(
        "{\"format\":1,\"platform\":\"windows\",\"arch\":\"x86_64\",\"revere_version\":\"0.0.41\",\"plugins\":[{\"id\":\"revere_cloud\",\"type\":\"plugin\",\"name\":\"Revere Cloud\",\"description\":\"Watch your cameras from anywhere with the Revere mobile app, and get alerts, clips and AI summaries. Your recordings stay on this computer; Revere Cloud syncs events, snapshots and recording times, and streams video only when you ask for it.\",\"source\":\"closed\",\"terms_url\":\"https://revere.video/terms\",\"terms_version\":1,\"file\":\"revere_cloud_system_plugin.dll\",\"plugin\":\"revere-v0.0.41-windows-cloud-plugin.dll\",\"manifest\":\"revere-v0.0.41-windows-cloud-plugin.dll.json\",\"signature\":\"revere-v0.0.41-windows-cloud-plugin.dll.json.sig\"},{\"id\":\"revere_homekit\",\"type\":\"plugin\",\"name\":\"Apple Home\",\"description\":\"Show your cameras in the Apple Home app on your iPhone, iPad and Mac, on your home network. Video goes straight from this computer to your Apple devices on your home network.\",\"source\":\"closed\",\"beta\":true,\"terms_url\":\"https://revere.video/terms\",\"terms_version\":1,\"file\":\"revere_homekit_system_plugin.dll\",\"plugin\":\"revere-v0.0.41-windows-homekit-plugin.dll\",\"manifest\":\"revere-v0.0.41-windows-homekit-plugin.dll.json\",\"signature\":\"revere-v0.0.41-windows-homekit-plugin.dll.json.sig\"}]}");
    RTF_ASSERT_EQUAL(c.plugins.size(), 2u);
    auto cloud = c.find("revere_cloud");
    RTF_ASSERT(cloud);
    RTF_ASSERT(cloud->source == r_catalog_source::closed);
    RTF_ASSERT(!cloud->beta);
    RTF_ASSERT_EQUAL(cloud->terms_version, 1u);
    RTF_ASSERT(cloud->file == "revere_cloud_system_plugin.dll");
    RTF_ASSERT(cloud->plugin == "revere-v0.0.41-windows-cloud-plugin.dll");
    auto home = c.find("revere_homekit");
    RTF_ASSERT(home);
    RTF_ASSERT(home->beta);
    RTF_ASSERT(home->file == "revere_homekit_system_plugin.dll");
    RTF_ASSERT(catalog_entry_assets("0.0.41", *home).signature_url == BASE + "revere-v0.0.41-windows-homekit-plugin.dll.json.sig");
}

void test_r_plugin_catalog::test_parse_rejects()
{
    RTF_ASSERT(_parse_throws("not json"));
    RTF_ASSERT(_parse_throws("[]"));
    RTF_ASSERT(_parse_throws(CATALOG_FORMAT2));
    RTF_ASSERT(_parse_throws(CATALOG_DUP_ID));
    RTF_ASSERT(_parse_throws(CATALOG_FOLDER));
    RTF_ASSERT(_parse_throws(_replace(CATALOG, "\"plugins\":[", "\"plugs\":[")));
    RTF_ASSERT(_parse_throws(_replace(CATALOG, "\"type\":\"link\"", "\"type\":\"theme\"")));
    RTF_ASSERT(_parse_throws(_replace(CATALOG, "\"source\":\"closed\"", "\"source\":\"secret\"")));
    RTF_ASSERT(_parse_throws(_replace(CATALOG, "\"id\":\"second\"", "\"id\":\"Second Plugin\"")));
    RTF_ASSERT(_parse_throws(_replace(CATALOG, "\"url\":\"https://example.test/homekit\"", "\"url\":\"http://example.test/homekit\"")));
    RTF_ASSERT(_parse_throws(_replace(CATALOG, "\"terms_url\":\"https://example.test/terms\"", "\"terms_url\":\"ftp://example.test/terms\"")));
    RTF_ASSERT(_parse_throws(_replace(CATALOG, "\"terms_version\":2", "\"terms_version\":0")));
    RTF_ASSERT(_parse_throws(_replace(CATALOG, "\"terms_version\":2", "\"terms_version\":\"2\"")));
    RTF_ASSERT(_parse_throws(_replace(CATALOG, "\"beta\":true", "\"beta\":\"yes\"")));
    // A link carries no assets.
    RTF_ASSERT(_parse_throws(_replace(CATALOG, "\"url\":\"https://example.test/homekit\"", "\"url\":\"https://example.test/homekit\",\"plugin\":\"x.dll\"")));
    // A plugin entry needs its assets.
    RTF_ASSERT(_parse_throws(_replace(CATALOG, "\"manifest\":\"revere-v0.0.41-windows-second-plugin.dll.json\",", "")));
}

void test_r_plugin_catalog::test_check_signature()
{
    r_plugin_catalog c;
    auto r = check_plugin_catalog(CATALOG, _sig(CATALOG_SIG_HEX), "0.0.41", {CATALOG_KEY}, "windows", "x86_64", c);
    RTF_ASSERT(r.ok());
    RTF_ASSERT_EQUAL(c.plugins.size(), 3u);

    // A key among several signs it.
    RTF_ASSERT(_check(CATALOG, CATALOG_SIG_HEX, {plugin_fixture::OTHER_KEY, CATALOG_KEY}).ok());

    // No signature, a wrong one, another key's, a changed catalog: refused, and nothing is set.
    r_plugin_catalog untouched;
    r = check_plugin_catalog(CATALOG, "", "0.0.41", {CATALOG_KEY}, "windows", "x86_64", untouched);
    RTF_ASSERT(r.status == r_plugin_check::bad_signature);
    RTF_ASSERT(untouched.plugins.empty());
    RTF_ASSERT(_check(CATALOG, CATALOG_FORMAT2_SIG_HEX).status == r_plugin_check::bad_signature);
    RTF_ASSERT(_check(CATALOG, CATALOG_SIG_HEX, {plugin_fixture::OTHER_KEY}).status == r_plugin_check::bad_signature);
    RTF_ASSERT(_check(CATALOG, CATALOG_SIG_HEX, {}).status == r_plugin_check::bad_signature);
    RTF_ASSERT(_check(_replace(CATALOG, "Revere Cloud", "Revere Clown"), CATALOG_SIG_HEX).status == r_plugin_check::bad_signature);
    RTF_ASSERT(_check(CATALOG + " ", CATALOG_SIG_HEX).status == r_plugin_check::bad_signature);

    // A key that signs plugin manifests only is trusted for catalogs only if it's in the list; this one signed nothing here.
    RTF_ASSERT(_check(CATALOG, CATALOG_SIG_HEX, {plugin_fixture::TEST_KEY}).status == r_plugin_check::bad_signature);
}

void test_r_plugin_catalog::test_check_rejects()
{
    // Signed, but not a catalog this revere takes.
    RTF_ASSERT(_check(CATALOG_FORMAT2, CATALOG_FORMAT2_SIG_HEX).status == r_plugin_check::malformed);
    RTF_ASSERT(_check(CATALOG_DUP_ID, CATALOG_DUP_ID_SIG_HEX).status == r_plugin_check::malformed);
    RTF_ASSERT(_check(CATALOG_FOLDER, CATALOG_FOLDER_SIG_HEX).status == r_plugin_check::malformed);
    // For another platform or version.
    RTF_ASSERT(_check(CATALOG_LINUX, CATALOG_LINUX_SIG_HEX).status == r_plugin_check::wrong_platform);
    RTF_ASSERT(_check(CATALOG_V42, CATALOG_V42_SIG_HEX).status == r_plugin_check::wrong_version);
    RTF_ASSERT(_check(CATALOG, CATALOG_SIG_HEX, {CATALOG_KEY}, "0.0.42").status == r_plugin_check::wrong_version);
    RTF_ASSERT(_check(CATALOG, CATALOG_SIG_HEX, {CATALOG_KEY}, "0.0.41", "linux").status == r_plugin_check::wrong_platform);
    // Too big to be one.
    RTF_ASSERT(_check(string(300 * 1024, ' '), CATALOG_SIG_HEX).status == r_plugin_check::malformed);
}

void test_r_plugin_catalog::test_assets()
{
    auto a = catalog_release_assets("0.0.41", "windows", "x86_64");
    RTF_ASSERT(a.manifest_url == BASE + "revere-v0.0.41-windows-plugins.json");
    RTF_ASSERT(a.signature_url == BASE + "revere-v0.0.41-windows-plugins.json.sig");
    RTF_ASSERT(a.plugin_url.empty());
    RTF_ASSERT(catalog_release_assets("0.0.41", "linux", "x86_64").manifest_url == BASE + "revere-v0.0.41-linux-plugins.json");
    RTF_ASSERT(catalog_release_assets("0.0.41", "linux", "arm64").manifest_url == BASE + "revere-v0.0.41-linux-arm64-plugins.json");
    RTF_ASSERT(catalog_release_assets("0.0.41", "macos", "arm64").manifest_url == BASE + "revere-v0.0.41-macos-plugins.json");
    RTF_ASSERT(catalog_release_assets("0.0.41", "macos", "x86_64").manifest_url == BASE + "revere-v0.0.41-macos-intel-plugins.json");

    auto e = catalog_entry_assets("0.0.41", *_catalog().find("second"));
    RTF_ASSERT(e.plugin_url == BASE + "revere-v0.0.41-windows-second-plugin.dll");
    RTF_ASSERT(e.manifest_url == BASE + "revere-v0.0.41-windows-second-plugin.dll.json");
    RTF_ASSERT(e.signature_url == BASE + "revere-v0.0.41-windows-second-plugin.dll.json.sig");
}

void test_r_plugin_catalog::test_choose_release()
{
    auto rel = [](const string& v, bool catalog, bool sig) {
        r_published_release r;
        r.version = v;
        if(catalog)
            r.assets.push_back("revere-v" + v + "-windows-plugins.json");
        if(sig)
            r.assets.push_back("revere-v" + v + "-windows-plugins.json.sig");
        return r;
    };
    vector<r_published_release> releases = {rel("0.0.43", true, true), rel("0.0.42", true, false), rel("0.0.41", true, true), rel("0.0.40", true, true), rel("0.0.39", false, false)};

    RTF_ASSERT(choose_catalog_release(releases, "0.0.43", "windows", "x86_64") == "0.0.43");
    // 0.0.42 has a catalog but no signature: not one.
    RTF_ASSERT(choose_catalog_release(releases, "0.0.42", "windows", "x86_64") == "0.0.41");
    RTF_ASSERT(choose_catalog_release(releases, "0.0.40", "windows", "x86_64") == "0.0.40");
    RTF_ASSERT(choose_catalog_release(releases, "0.0.39", "windows", "x86_64").empty());
    RTF_ASSERT(choose_catalog_release(releases, "0.0.38", "windows", "x86_64").empty());
    // Another platform has none; a base that isn't x.y.z names none.
    RTF_ASSERT(choose_catalog_release(releases, "0.0.43", "linux", "x86_64").empty());
    RTF_ASSERT(choose_catalog_release(releases, "0.0.43-dirty", "windows", "x86_64").empty());
    RTF_ASSERT(choose_catalog_release(releases, "", "windows", "x86_64").empty());
}

void test_r_plugin_catalog::test_fetch_release_build()
{
    fake_server server;
    auto r = _fetch(server);
    RTF_ASSERT(r.ok());
    RTF_ASSERT(r.release_version == "0.0.41");
    RTF_ASSERT_EQUAL(r.catalog.plugins.size(), 3u);
    // The catalog and its signature, nothing else.
    RTF_ASSERT_EQUAL(server.fetched.size(), 2u);

    // A bad signature is refused, and says so.
    fake_server bad;
    bad.bodies[CATALOG_URL + ".sig"] = _sig(CATALOG_FORMAT2_SIG_HEX);
    auto b = _fetch(bad);
    RTF_ASSERT(b.status == r_plugin_install::check_failed);
    RTF_ASSERT(b.check == r_plugin_check::bad_signature);
    RTF_ASSERT(b.catalog.plugins.empty());

    // A catalog changed after signing.
    fake_server changed;
    changed.bodies[CATALOG_URL] = _replace(CATALOG, "\"beta\":true", "\"beta\":false");
    RTF_ASSERT(_fetch(changed).status == r_plugin_install::check_failed);

    // No signature file at all.
    fake_server unsigned_;
    unsigned_.bodies.erase(CATALOG_URL + ".sig");
    RTF_ASSERT(_fetch(unsigned_).status == r_plugin_install::download_failed);

    // A catalog for another release than the one it's fetched from.
    fake_server other;
    other.bodies[CATALOG_URL] = CATALOG_V42;
    other.bodies[CATALOG_URL + ".sig"] = _sig(CATALOG_V42_SIG_HEX);
    auto o = _fetch(other);
    RTF_ASSERT(o.status == r_plugin_install::check_failed);
    RTF_ASSERT(o.check == r_plugin_check::wrong_version);

    // No network.
    fake_server offline;
    offline.offline = true;
    RTF_ASSERT(_fetch(offline).status == r_plugin_install::download_failed);

    // A build that names no release.
    fake_server any;
    RTF_ASSERT(_fetch(any, "unknown").status == r_plugin_install::not_available);
    RTF_ASSERT(any.fetched.empty());
}

void test_r_plugin_catalog::test_fetch_dev_build()
{
    fake_server server;
    server.bodies[REVERE_RELEASES_API_URL] =
        "[{\"tag_name\":\"v0.0.43\",\"assets\":[{\"name\":\"revere-v0.0.43-windows-plugins.json\"}]},"
        "{\"tag_name\":\"v0.0.41\",\"assets\":[{\"name\":\"revere-v0.0.41-windows-plugins.json\"},"
        "{\"name\":\"revere-v0.0.41-windows-plugins.json.sig\"}]}]";

    // 0.0.42-dirty: the newest at or below 0.0.42 with a signed catalog is 0.0.41's, and it's for exactly 0.0.41.
    auto r = _fetch(server, "0.0.42-dirty");
    RTF_ASSERT(r.ok());
    RTF_ASSERT(r.release_version == "0.0.41");
    RTF_ASSERT(r.catalog.revere_version == "0.0.41");

    // Nothing at or below it.
    auto none = _fetch(server, "0.0.40-3-gabc1234");
    RTF_ASSERT(none.status == r_plugin_install::not_available);

    // GitHub down for the list.
    fake_server no_list;
    auto down = _fetch(no_list, "0.0.42-dirty");
    RTF_ASSERT(down.status == r_plugin_install::download_failed);
}

void test_r_plugin_catalog::test_fetch_missing_for_platform()
{
    // The release has no catalog for linux: not_available, so the Store says plugins aren't offered here.
    fake_server server;
    auto r = _fetch(server, "0.0.41", "linux");
    RTF_ASSERT(r.status == r_plugin_install::not_available);

    // A windows catalog served where linux's belongs is refused: signed, but for another platform.
    server.bodies[BASE + "revere-v0.0.41-linux-plugins.json"] = CATALOG;
    server.bodies[BASE + "revere-v0.0.41-linux-plugins.json.sig"] = _sig(CATALOG_SIG_HEX);
    auto wrong = _fetch(server, "0.0.41", "linux");
    RTF_ASSERT(wrong.status == r_plugin_install::check_failed);
    RTF_ASSERT(wrong.check == r_plugin_check::wrong_platform);
}

void test_r_plugin_catalog::test_state()
{
    auto c = _catalog();
    auto cloud = *c.find("revere_cloud");   // test_system_plugin.dll
    string detail;

    RTF_ASSERT(plugin_entry_state(cloud, {}, {}, s_dir.string(), detail) == r_plugin_state::not_installed);
    RTF_ASSERT(detail.empty());
    RTF_ASSERT(plugin_entry_state(cloud, {"test_system_plugin"}, {}, s_dir.string(), detail) == r_plugin_state::installed);
    // Another plugin's loading doesn't count.
    RTF_ASSERT(plugin_entry_state(cloud, {"second_system_plugin"}, {}, s_dir.string(), detail) == r_plugin_state::not_installed);

    // Skipped: the checks that mean "update it" and the rest.
    for(auto status : {r_plugin_check::no_manifest, r_plugin_check::wrong_version, r_plugin_check::wrong_plugin_api, r_plugin_check::wont_load})
    {
        RTF_ASSERT(plugin_entry_state(cloud, {}, {_skipped("test_system_plugin", status)}, s_dir.string(), detail) == r_plugin_state::needs_update);
        RTF_ASSERT(detail == string(plugin_check_name(status)) + ": why");
    }
    for(auto status : {r_plugin_check::bad_signature, r_plugin_check::hash_mismatch, r_plugin_check::size_mismatch, r_plugin_check::malformed})
        RTF_ASSERT(plugin_entry_state(cloud, {}, {_skipped("test_system_plugin", status)}, s_dir.string(), detail) == r_plugin_state::failed_check);
    // Another plugin's skip doesn't count.
    RTF_ASSERT(plugin_entry_state(cloud, {}, {_skipped("other", r_plugin_check::bad_signature)}, s_dir.string(), detail) == r_plugin_state::not_installed);

    // The user chose to uninstall it: the marker wins over loaded.
    fs::create_directories(s_dir);
    auto plugin_path = (s_dir / cloud.file).string();
    { ofstream f(plugin_uninstall_marker_path(plugin_path)); f << "uninstall\n"; }
    RTF_ASSERT(plugin_entry_state(cloud, {"test_system_plugin"}, {}, s_dir.string(), detail) == r_plugin_state::removal_pending);
    RTF_ASSERT(plugin_entry_state(*c.find("second"), {"second_system_plugin"}, {}, s_dir.string(), detail) == r_plugin_state::installed);

    // A link has no state to speak of.
    RTF_ASSERT(plugin_entry_state(*c.find("homekit"), {}, {}, s_dir.string(), detail) == r_plugin_state::not_installed);

    RTF_ASSERT(string(plugin_state_name(r_plugin_state::removal_pending)) == "removal_pending");
    RTF_ASSERT(string(plugin_state_name(r_plugin_state::needs_update)) == "needs_update");
}

void test_r_plugin_catalog::test_install_second_plugin()
{
    auto c = _catalog();
    fake_server server;
    auto r = install_catalog_plugin(*c.find("second"), "0.0.41", s_dir.string(), server.fetch(), _keys(), "windows", "x86_64");
    RTF_ASSERT(r.ok());
    RTF_ASSERT(r.release_version == "0.0.41");
    RTF_ASSERT(_read(r.path) == SECOND_BYTES);
    RTF_ASSERT(fs::path(r.path).filename() == "second_system_plugin.dll");
    RTF_ASSERT(_read(plugin_manifest_path(r.path)) == SECOND_MANIFEST);
    RTF_ASSERT(_read(plugin_signature_path(r.path)) == _sig(SECOND_MANIFEST_SIG_HEX));

    // Revere Cloud, as the first entry, goes the same way.
    auto cloud = install_catalog_plugin(*c.find("revere_cloud"), "0.0.41", s_dir.string(), server.fetch(), _keys(), "windows", "x86_64");
    RTF_ASSERT(cloud.ok());
    RTF_ASSERT(_read(cloud.path) == plugin_fixture::PLUGIN_BYTES);

    // The plugin's own manifest is still checked: signed by a key that isn't trusted, nothing's written.
    fs::remove_all(s_dir);
    auto untrusted = install_catalog_plugin(*c.find("second"), "0.0.41", s_dir.string(), server.fetch(), {plugin_fixture::TEST_KEY}, "windows", "x86_64");
    RTF_ASSERT(untrusted.status == r_plugin_install::check_failed);
    RTF_ASSERT(untrusted.check == r_plugin_check::bad_signature);
    RTF_ASSERT(!fs::exists(s_dir / "second_system_plugin.dll"));

    // A manifest for another file than the entry's.
    fake_server swapped;
    swapped.bodies[BASE + "revere-v0.0.41-windows-second-plugin.dll.json"] = plugin_fixture::MANIFEST;
    swapped.bodies[BASE + "revere-v0.0.41-windows-second-plugin.dll.json.sig"] = _sig(plugin_fixture::MANIFEST_SIG_HEX);
    auto wrong = install_catalog_plugin(*c.find("second"), "0.0.41", s_dir.string(), swapped.fetch(), _keys(), "windows", "x86_64");
    RTF_ASSERT(wrong.status == r_plugin_install::check_failed);
    RTF_ASSERT(wrong.check == r_plugin_check::wrong_file);

    // A plugin changed after it was signed: the same size, then a bigger one (never read past the manifest's size).
    fake_server tampered;
    tampered.bodies[BASE + "revere-v0.0.41-windows-second-plugin.dll"] = "Second plugin\n";
    RTF_ASSERT(install_catalog_plugin(*c.find("second"), "0.0.41", s_dir.string(), tampered.fetch(), _keys(), "windows", "x86_64").check ==
               r_plugin_check::hash_mismatch);
    tampered.bodies[BASE + "revere-v0.0.41-windows-second-plugin.dll"] = "second plugin!\n";
    RTF_ASSERT(install_catalog_plugin(*c.find("second"), "0.0.41", s_dir.string(), tampered.fetch(), _keys(), "windows", "x86_64").status ==
               r_plugin_install::download_failed);

    // The release has no such plugin.
    fake_server missing;
    missing.bodies.erase(BASE + "revere-v0.0.41-windows-second-plugin.dll.json");
    RTF_ASSERT(install_catalog_plugin(*c.find("second"), "0.0.41", s_dir.string(), missing.fetch(), _keys(), "windows", "x86_64").status ==
               r_plugin_install::not_available);
    RTF_ASSERT(!fs::exists(s_dir / "second_system_plugin.dll"));
}

void test_r_plugin_catalog::test_install_link_throws()
{
    auto c = _catalog();
    fake_server server;
    bool threw = false;
    try { install_catalog_plugin(*c.find("homekit"), "0.0.41", s_dir.string(), server.fetch(), _keys(), "windows", "x86_64"); }
    catch(const r_invalid_argument_exception&) { threw = true; }
    RTF_ASSERT(threw);
    RTF_ASSERT(server.fetched.empty());
    threw = false;
    try { catalog_entry_assets("0.0.41", *c.find("homekit")); }
    catch(const r_invalid_argument_exception&) { threw = true; }
    RTF_ASSERT(threw);
}

void test_r_plugin_catalog::test_terms()
{
    auto c = _catalog();
    auto cloud = *c.find("revere_cloud");   // terms version 2
    auto second = *c.find("second");        // no terms
    auto path = (s_dir.parent_path() / "plugin_terms.json").string();
    fs::create_directories(s_dir.parent_path());
    string why;

    {
        r_plugin_terms terms(path);   // no file: nothing accepted
        RTF_ASSERT_EQUAL(terms.accepted_version("revere_cloud"), 0u);
        RTF_ASSERT(terms.needs_acceptance(cloud));
        RTF_ASSERT(!terms.needs_acceptance(second));

        RTF_ASSERT(terms.accept("revere_cloud", 1, why));
        RTF_ASSERT(terms.needs_acceptance(cloud));   // version 1 isn't 2
        RTF_ASSERT(terms.accept("revere_cloud", 2, why));
        RTF_ASSERT(!terms.needs_acceptance(cloud));
        // An older version doesn't take the newer back.
        RTF_ASSERT(terms.accept("revere_cloud", 1, why));
        RTF_ASSERT_EQUAL(terms.accepted_version("revere_cloud"), 2u);
        RTF_ASSERT(terms.accept("second", 5, why));
    }

    // Kept across a restart, per plugin, with the time.
    {
        r_plugin_terms terms(path);
        RTF_ASSERT_EQUAL(terms.accepted_version("revere_cloud"), 2u);
        RTF_ASSERT_EQUAL(terms.accepted_version("second"), 5u);
        RTF_ASSERT(!terms.needs_acceptance(cloud));
        auto text = _read(path);
        RTF_ASSERT(text.find("\"accepted_at\"") != string::npos);
        RTF_ASSERT(text.find("T") != string::npos);

        // The terms change: a higher version asks again.
        auto newer = cloud;
        newer.terms_version = 3;
        RTF_ASSERT(terms.needs_acceptance(newer));
        RTF_ASSERT(terms.accept("revere_cloud", 3, why));
        RTF_ASSERT(!terms.needs_acceptance(newer));
    }

    // An unreadable file means nothing accepted.
    { ofstream f(path, ios::trunc); f << "{ not json"; }
    r_plugin_terms broken(path);
    RTF_ASSERT_EQUAL(broken.accepted_version("revere_cloud"), 0u);
    RTF_ASSERT(broken.needs_acceptance(cloud));

    // A file that can't be written records nothing.
    r_plugin_terms nowhere((s_dir.parent_path() / "no_such_dir" / "x" / "terms.json").string());
    RTF_ASSERT(!nowhere.accept("revere_cloud", 2, why));
    RTF_ASSERT(!why.empty());
    RTF_ASSERT(nowhere.needs_acceptance(cloud));
}
