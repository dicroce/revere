#include "test_r_cloud_install.h"
#include "r_vss/r_cloud_install.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

using namespace r_vss;
using namespace std;
namespace fs = std::filesystem;

REGISTER_TEST_FIXTURE(test_r_cloud_install);

namespace
{

fs::path s_dir;

// A fake plugin host and installer.
struct fake_host
{
    mutex lock;
    vector<string> loaded;
    vector<r_skipped_system_plugin> skipped;
    r_plugin_install_result install_result;
    bool load_ok {true};
    atomic<int> installs {0};
    vector<string> load_paths;
    atomic<bool> hold {false};   // keeps an install running until cleared

    fake_host()
    {
        install_result.status = r_plugin_install::ok;
        install_result.path = "/plugins/revere_cloud_system_plugin.dll";
    }

    r_cloud_install::deps deps(bool release = true)
    {
        r_cloud_install::deps d;
        d.install_dir = "/plugins";
        d.terms_path = (s_dir / "revere_cloud_terms.json").string();
        d.release_build = release;
        d.install = [this](const string& dir) {
            ++installs;
            while(hold)
                this_thread::sleep_for(chrono::milliseconds(5));
            auto r = install_result;
            if(r.ok())
                r.path = dir + "/revere_cloud_system_plugin.dll";
            return r;
        };
        d.load = [this](const string& path, string& why) {
            lock_guard<mutex> g(lock);
            load_paths.push_back(path);
            if(!load_ok)
            {
                why = "not a system plugin";
                return false;
            }
            loaded.push_back(REVERE_CLOUD_PLUGIN_NAME);
            skipped.clear();
            return true;
        };
        d.loaded = [this]() { lock_guard<mutex> g(lock); return loaded; };
        d.skipped = [this]() { lock_guard<mutex> g(lock); return skipped; };
        return d;
    }
};

r_skipped_system_plugin _skipped(r_plugin_check status, const string& detail)
{
    r_skipped_system_plugin s;
    s.name = REVERE_CLOUD_PLUGIN_NAME;
    s.path = "/plugins/revere_cloud_system_plugin.dll";
    s.check.status = status;
    s.check.detail = detail;
    return s;
}

// Waits for a started install to finish.
r_cloud_install_status _wait(r_cloud_install& ci)
{
    for(int i = 0; i < 1000; ++i)
    {
        auto s = ci.status();
        if(s.state != r_cloud_state::installing)
            return s;
        this_thread::sleep_for(chrono::milliseconds(5));
    }
    return ci.status();
}

string _read(const fs::path& p)
{
    ifstream f(p);
    stringstream s;
    s << f.rdbuf();
    return s.str();
}

}

void test_r_cloud_install::setup()
{
    auto stamp = chrono::steady_clock::now().time_since_epoch().count();
    s_dir = fs::temp_directory_path() / ("r_cloud_install_ut_" + to_string(stamp));
    fs::create_directories(s_dir);
}

void test_r_cloud_install::teardown()
{
    error_code ec;
    fs::remove_all(s_dir, ec);
}

void test_r_cloud_install::test_release_version()
{
    RTF_ASSERT(is_release_version("0.0.41"));
    RTF_ASSERT(is_release_version("10.20.300"));
    RTF_ASSERT(!is_release_version("0.0.40-4-gabc1234"));
    RTF_ASSERT(!is_release_version("abc1234"));
    RTF_ASSERT(!is_release_version("unknown"));
    RTF_ASSERT(!is_release_version("0.0"));
    RTF_ASSERT(!is_release_version("0.0.41."));
    RTF_ASSERT(!is_release_version("0.0.41.1"));
    RTF_ASSERT(!is_release_version(""));
}

void test_r_cloud_install::test_states()
{
    fake_host host;
    r_cloud_install ci(host.deps());
    auto s = ci.status();
    RTF_ASSERT(s.state == r_cloud_state::not_installed);
    RTF_ASSERT(s.can_install);
    RTF_ASSERT(!s.terms_accepted);
    RTF_ASSERT(s.error_kind.empty());

    // Found but skipped: from an older installer or revere, or tampered with.
    host.skipped = {_skipped(r_plugin_check::no_manifest, "no manifest")};
    s = ci.status();
    RTF_ASSERT(s.state == r_cloud_state::needs_update);
    RTF_ASSERT(s.detail == "no_manifest: no manifest");
    RTF_ASSERT(s.can_install);
    host.skipped = {_skipped(r_plugin_check::wrong_version, "built for revere 0.0.40")};
    RTF_ASSERT(ci.status().state == r_cloud_state::needs_update);
    host.skipped = {_skipped(r_plugin_check::hash_mismatch, "")};
    s = ci.status();
    RTF_ASSERT(s.state == r_cloud_state::failed_check);
    RTF_ASSERT(s.detail == "hash_mismatch");

    // Another plugin's skip isn't Revere Cloud's.
    auto other = _skipped(r_plugin_check::hash_mismatch, "");
    other.name = "other_system_plugin";
    host.skipped = {other};
    RTF_ASSERT(ci.status().state == r_cloud_state::not_installed);

    // Loaded: installed, and nothing to install.
    host.loaded = {REVERE_CLOUD_PLUGIN_NAME};
    s = ci.status();
    RTF_ASSERT(s.state == r_cloud_state::installed);
    RTF_ASSERT(!s.can_install);

    // A build that isn't a release can't install it itself.
    fake_host dev;
    r_cloud_install dev_ci(dev.deps(false));
    RTF_ASSERT(!dev_ci.status().can_install);
    RTF_ASSERT(dev_ci.status().state == r_cloud_state::not_installed);
}

void test_r_cloud_install::test_start_refused()
{
    string why;
    {
        fake_host host;
        r_cloud_install ci(host.deps(false));
        RTF_ASSERT(!ci.start(REVERE_CLOUD_TERMS_VERSION, why));
        RTF_ASSERT(why.find("release build") != string::npos);
    }
    {
        // The terms not accepted, or an older version of them.
        fake_host host;
        r_cloud_install ci(host.deps());
        RTF_ASSERT(!ci.start("", why));
        RTF_ASSERT(why.find("terms") != string::npos);
        RTF_ASSERT(!ci.start("2020-01-01", why));
        RTF_ASSERT(!fs::exists(s_dir / "revere_cloud_terms.json"));
        RTF_ASSERT_EQUAL(host.installs.load(), 0);
    }
    {
        fake_host host;
        host.loaded = {REVERE_CLOUD_PLUGIN_NAME};
        r_cloud_install ci(host.deps());
        RTF_ASSERT(!ci.start(REVERE_CLOUD_TERMS_VERSION, why));
        RTF_ASSERT(why.find("already installed") != string::npos);
    }
    {
        // One at a time.
        fake_host host;
        host.hold = true;
        r_cloud_install ci(host.deps());
        RTF_ASSERT(ci.start(REVERE_CLOUD_TERMS_VERSION, why));
        RTF_ASSERT(ci.status().state == r_cloud_state::installing);
        RTF_ASSERT(!ci.status().can_install);
        RTF_ASSERT(!ci.start(REVERE_CLOUD_TERMS_VERSION, why));
        RTF_ASSERT(why.find("already being installed") != string::npos);
        host.hold = false;
        RTF_ASSERT(_wait(ci).state == r_cloud_state::installed);
    }
}

void test_r_cloud_install::test_install_and_load()
{
    // An older plugin skipped for another revere version; the update installs into
    // the user folder, loads it, and records the terms.
    fake_host host;
    host.skipped = {_skipped(r_plugin_check::wrong_version, "built for revere 0.0.40")};
    r_cloud_install ci(host.deps());
    string why;
    RTF_ASSERT(ci.start(REVERE_CLOUD_TERMS_VERSION, why));
    auto s = _wait(ci);
    RTF_ASSERT(s.state == r_cloud_state::installed);
    RTF_ASSERT(s.error_kind.empty());
    RTF_ASSERT(s.terms_accepted);
    RTF_ASSERT_EQUAL(host.installs.load(), 1);
    RTF_ASSERT_EQUAL(host.load_paths.size(), (size_t)1);
    RTF_ASSERT(host.load_paths[0] == "/plugins/revere_cloud_system_plugin.dll");

    auto terms = _read(s_dir / "revere_cloud_terms.json");
    RTF_ASSERT(terms.find(REVERE_CLOUD_TERMS_VERSION) != string::npos);
    RTF_ASSERT(terms.find("accepted_at") != string::npos);
}

void test_r_cloud_install::test_failed_install()
{
    fake_host host;
    host.install_result.status = r_plugin_install::check_failed;
    host.install_result.check = r_plugin_check::bad_signature;
    host.install_result.detail = "bad_signature: no trusted key signed the manifest";
    r_cloud_install ci(host.deps());
    string why;
    RTF_ASSERT(ci.start(REVERE_CLOUD_TERMS_VERSION, why));
    auto s = _wait(ci);
    RTF_ASSERT(s.state == r_cloud_state::not_installed);
    RTF_ASSERT(s.error_kind == "check_failed");
    RTF_ASSERT(s.error_detail.find("no trusted key") != string::npos);
    RTF_ASSERT(s.can_install);
    RTF_ASSERT(host.load_paths.empty());

    // Installed but not loadable.
    host.install_result.status = r_plugin_install::ok;
    host.load_ok = false;
    RTF_ASSERT(ci.start("", why));   // the terms were accepted the first time
    s = _wait(ci);
    RTF_ASSERT(s.error_kind == "load_failed");
    RTF_ASSERT(s.error_detail == "not a system plugin");

    // A try that works clears the error.
    host.load_ok = true;
    RTF_ASSERT(ci.start("", why));
    s = _wait(ci);
    RTF_ASSERT(s.state == r_cloud_state::installed);
    RTF_ASSERT(s.error_kind.empty());
    RTF_ASSERT(s.error_detail.empty());
}

void test_r_cloud_install::test_terms_kept()
{
    string why;
    {
        fake_host host;
        r_cloud_install ci(host.deps());
        RTF_ASSERT(ci.start(REVERE_CLOUD_TERMS_VERSION, why));
        _wait(ci);
    }
    {
        // Another run of revere remembers them.
        fake_host host;
        r_cloud_install ci(host.deps());
        RTF_ASSERT(ci.status().terms_accepted);
        RTF_ASSERT(ci.start("", why));
        _wait(ci);
    }
    {
        // Terms accepted at an older version must be accepted again.
        ofstream(s_dir / "revere_cloud_terms.json") << "{\"version\": \"2020-01-01\", \"accepted_at\": \"2020-01-01T00:00:00Z\"}";
        fake_host host;
        r_cloud_install ci(host.deps());
        RTF_ASSERT(!ci.status().terms_accepted);
        RTF_ASSERT(!ci.start("", why));
    }
    {
        // A file that isn't json is no acceptance, and no crash.
        ofstream(s_dir / "revere_cloud_terms.json") << "not json";
        fake_host host;
        r_cloud_install ci(host.deps());
        RTF_ASSERT(!ci.status().terms_accepted);
    }
}
