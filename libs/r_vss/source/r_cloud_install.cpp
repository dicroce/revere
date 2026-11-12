
#include "r_vss/r_cloud_install.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/3rdparty/json/json.h"
#include <algorithm>
#include <cctype>
#include <chrono>

using namespace r_vss;
using namespace r_utils;
using namespace std;

bool r_vss::is_release_version(const string& version)
{
    int parts = 0;
    size_t i = 0;
    while(i < version.size())
    {
        size_t start = i;
        while(i < version.size() && isdigit((unsigned char)version[i]))
            ++i;
        if(i == start)
            return false;
        ++parts;
        if(i == version.size())
            break;
        if(version[i] != '.' || parts == 3)
            return false;
        ++i;
        if(i == version.size())
            return false;
    }
    return parts == 3;
}

const char* r_vss::cloud_state_name(r_cloud_state state)
{
    switch(state)
    {
        case r_cloud_state::not_installed: return "not_installed";
        case r_cloud_state::installing: return "installing";
        case r_cloud_state::installed: return "installed";
        case r_cloud_state::needs_update: return "needs_update";
        case r_cloud_state::failed_check: return "failed_check";
    }
    return "unknown";
}

r_cloud_install::r_cloud_install(const deps& d) :
    _deps(d)
{
    // {"version": "2026-11-07", "accepted_at": "2026-11-12T17:30:00Z"}
    try
    {
        if(!_deps.terms_path.empty() && r_fs::file_exists(_deps.terms_path))
        {
            auto bytes = r_fs::read_file(_deps.terms_path);
            auto j = nlohmann::json::parse(string(bytes.begin(), bytes.end()), nullptr, false);
            if(j.is_object() && j.contains("version") && j["version"].is_string())
                _accepted_terms_version = j["version"].get<string>();
        }
    }
    catch(const exception& e)
    {
        R_LOG_WARNING("Could not read %s: %s", _deps.terms_path.c_str(), e.what());
    }
}

r_cloud_install::~r_cloud_install()
{
    if(_thread.joinable())
        _thread.join();
}

bool r_cloud_install::_terms_accepted_locked() const
{
    return _accepted_terms_version == REVERE_CLOUD_TERMS_VERSION;
}

r_cloud_install_status r_cloud_install::status() const
{
    bool installing;
    r_cloud_install_status s;
    {
        lock_guard<mutex> g(_lock);
        installing = _installing;
        s.terms_accepted = _terms_accepted_locked();
        s.error_kind = _error_kind;
        s.error_detail = _error_detail;
    }

    auto loaded = _deps.loaded ? _deps.loaded() : vector<string>();
    bool is_loaded = find(loaded.begin(), loaded.end(), REVERE_CLOUD_PLUGIN_NAME) != loaded.end();

    if(installing)
        s.state = r_cloud_state::installing;
    else if(is_loaded)
        s.state = r_cloud_state::installed;
    else
    {
        s.state = r_cloud_state::not_installed;
        for(const auto& sk : (_deps.skipped ? _deps.skipped() : vector<r_skipped_system_plugin>()))
        {
            if(sk.name != REVERE_CLOUD_PLUGIN_NAME)
                continue;
            s.state = sk.needs_update() ? r_cloud_state::needs_update : r_cloud_state::failed_check;
            s.detail = string(plugin_check_name(sk.check.status)) + (sk.check.detail.empty() ? string() : ": " + sk.check.detail);
        }
    }
    s.can_install = _deps.release_build && !installing && !is_loaded;
    return s;
}

bool r_cloud_install::start(const string& accepted_terms_version, string& why)
{
    lock_guard<mutex> g(_lock);
    if(!_deps.release_build)
    {
        why = "only a release build of revere installs Revere Cloud itself";
        return false;
    }
    if(_installing)
    {
        why = "Revere Cloud is already being installed";
        return false;
    }
    auto loaded = _deps.loaded ? _deps.loaded() : vector<string>();
    if(find(loaded.begin(), loaded.end(), REVERE_CLOUD_PLUGIN_NAME) != loaded.end())
    {
        why = "Revere Cloud is already installed";
        return false;
    }
    if(accepted_terms_version != REVERE_CLOUD_TERMS_VERSION && !_terms_accepted_locked())
    {
        why = string("the Revere Cloud terms (version ") + REVERE_CLOUD_TERMS_VERSION + ") must be accepted first";
        return false;
    }

    if(!_terms_accepted_locked())
    {
        nlohmann::json j = {{"version", REVERE_CLOUD_TERMS_VERSION},
                            {"accepted_at", r_time_utils::tp_to_iso_8601(chrono::system_clock::now(), true)}};
        auto text = j.dump(2);
        try
        {
            r_fs::atomic_write_file(reinterpret_cast<const uint8_t*>(text.data()), text.size(), _deps.terms_path);
        }
        catch(const exception& e)
        {
            why = string("could not record the terms acceptance: ") + e.what();
            return false;
        }
        _accepted_terms_version = REVERE_CLOUD_TERMS_VERSION;
        R_LOG_INFO("Revere Cloud terms version %s accepted", REVERE_CLOUD_TERMS_VERSION);
    }

    if(_thread.joinable())
        _thread.join();   // the last install's thread, already done (_installing is false)
    _installing = true;
    _error_kind.clear();
    _error_detail.clear();
    _thread = std::thread(&r_cloud_install::_run, this);
    return true;
}

void r_cloud_install::_run()
{
    string kind, detail;
    try
    {
        R_LOG_INFO("Installing Revere Cloud into %s", _deps.install_dir.c_str());
        auto r = _deps.install(_deps.install_dir);
        if(!r.ok())
        {
            kind = plugin_install_name(r.status);
            detail = r.detail;
        }
        else
        {
            string why;
            if(!_deps.load(r.path, why))
            {
                kind = "load_failed";
                detail = why;
            }
        }
    }
    catch(const exception& e)
    {
        kind = "download_failed";
        detail = e.what();
    }

    if(kind.empty())
        R_LOG_INFO("Revere Cloud installed and started");
    else
        R_LOG_ERROR("Revere Cloud install failed (%s): %s", kind.c_str(), detail.c_str());

    lock_guard<mutex> g(_lock);
    _error_kind = kind;
    _error_detail = detail;
    _installing = false;
}
