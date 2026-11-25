#include "r_vss/r_plugin_catalog.h"
#include "r_vss/r_build_info.h"
#include "r_utils/r_ecdsa.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"
#include "r_utils/3rdparty/json/json.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>

using namespace r_vss;
using namespace r_utils;
using namespace std;
namespace fs = std::filesystem;

namespace
{

constexpr size_t MAX_CATALOG_BYTES = 256 * 1024;
constexpr size_t MAX_SIGNATURE_BYTES = 1024;
constexpr size_t MAX_RELEASES_BYTES = 16 * 1024 * 1024;
constexpr uint64_t CATALOG_FETCH_TIMEOUT_MS = 15000;
constexpr const char* RELEASE_DOWNLOADS = "https://github.com/dicroce/revere/releases/download/v";

r_plugin_check_result _check(r_plugin_check status, const string& detail = string())
{
    r_plugin_check_result r;
    r.status = status;
    r.detail = detail;
    return r;
}

bool _plain_file_name(const string& s)
{
    return !s.empty() && s.find_first_of("/\\:") == string::npos && s != "." && s != "..";
}

bool _plain_id(const string& s)
{
    if(s.empty() || s.size() > 40)
        return false;
    for(char c : s)
    {
        if(!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            return false;
    }
    return true;
}

bool _https(const string& s)
{
    return s.size() > 8 && s.compare(0, 8, "https://") == 0 && s.find_first_of(" \t\r\n") == string::npos;
}

string _utc_now()
{
    auto t = chrono::system_clock::to_time_t(chrono::system_clock::now());
    tm utc {};
#ifdef _WIN32
    gmtime_s(&utc, &t);
#else
    gmtime_r(&t, &utc);
#endif
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buf;
}

r_catalog_fetch_result _fetch_result(r_plugin_install status, const string& detail, r_plugin_check check = r_plugin_check::unreadable)
{
    r_catalog_fetch_result r;
    r.status = status;
    r.check = check;
    r.detail = detail;
    return r;
}

}

const r_catalog_entry* r_plugin_catalog::find(const string& id) const
{
    for(const auto& e : plugins)
    {
        if(e.id == id)
            return &e;
    }
    return nullptr;
}

r_plugin_catalog r_vss::parse_plugin_catalog(const string& json)
{
    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(json);
    }
    catch(const exception& e)
    {
        R_STHROW(r_invalid_argument_exception, ("Plugin catalog isn't json: %s", e.what()));
    }
    if(!j.is_object())
        R_STHROW(r_invalid_argument_exception, ("Plugin catalog isn't a json object."));

    auto str = [](const nlohmann::json& o, const char* key) {
        auto it = o.find(key);
        if(it == o.end() || !it->is_string())
            R_STHROW(r_invalid_argument_exception, ("Plugin catalog has no string \"%s\".", key));
        return it->get<string>();
    };
    auto opt_str = [](const nlohmann::json& o, const char* key) {
        auto it = o.find(key);
        if(it == o.end())
            return string();
        if(!it->is_string())
            R_STHROW(r_invalid_argument_exception, ("Plugin catalog's \"%s\" isn't a string.", key));
        return it->get<string>();
    };

    r_plugin_catalog c;
    auto format = j.find("format");
    if(format == j.end() || !format->is_number_unsigned() || format->get<uint64_t>() != 1)
        R_STHROW(r_invalid_argument_exception, ("Plugin catalog format isn't one this revere reads (1)."));
    c.format = 1;
    c.platform = str(j, "platform");
    c.arch = str(j, "arch");
    c.revere_version = str(j, "revere_version");
    auto plugins = j.find("plugins");
    if(plugins == j.end() || !plugins->is_array())
        R_STHROW(r_invalid_argument_exception, ("Plugin catalog has no \"plugins\" array."));

    for(const auto& p : *plugins)
    {
        if(!p.is_object())
            R_STHROW(r_invalid_argument_exception, ("A plugin catalog entry isn't a json object."));
        r_catalog_entry e;
        e.id = str(p, "id");
        if(!_plain_id(e.id))
            R_STHROW(r_invalid_argument_exception, ("Plugin catalog id \"%s\" isn't 1 to 40 of a-z, 0-9 and _.", e.id.c_str()));
        if(c.find(e.id))
            R_STHROW(r_invalid_argument_exception, ("Plugin catalog has \"%s\" twice.", e.id.c_str()));
        e.name = str(p, "name");
        e.description = str(p, "description");
        if(e.name.empty())
            R_STHROW(r_invalid_argument_exception, ("Plugin catalog entry \"%s\" has no name.", e.id.c_str()));

        auto type = str(p, "type");
        if(type == "plugin")
            e.type = r_catalog_entry_type::plugin;
        else if(type == "link")
            e.type = r_catalog_entry_type::link;
        else
            R_STHROW(r_invalid_argument_exception, ("Plugin catalog entry \"%s\" has type \"%s\", not plugin or link.", e.id.c_str(), type.c_str()));

        auto source = opt_str(p, "source");
        if(source == "open" || source.empty())
            e.source = r_catalog_source::open;
        else if(source == "closed")
            e.source = r_catalog_source::closed;
        else
            R_STHROW(r_invalid_argument_exception, ("Plugin catalog entry \"%s\" has source \"%s\", not open or closed.", e.id.c_str(), source.c_str()));

        auto beta = p.find("beta");
        if(beta != p.end())
        {
            if(!beta->is_boolean())
                R_STHROW(r_invalid_argument_exception, ("Plugin catalog entry \"%s\": beta isn't true or false.", e.id.c_str()));
            e.beta = beta->get<bool>();
        }

        e.terms_url = opt_str(p, "terms_url");
        if(!e.terms_url.empty() && !_https(e.terms_url))
            R_STHROW(r_invalid_argument_exception, ("Plugin catalog entry \"%s\": terms_url isn't an https url.", e.id.c_str()));
        auto terms_version = p.find("terms_version");
        if(terms_version != p.end())
        {
            if(!terms_version->is_number_unsigned() || terms_version->get<uint64_t>() < 1 || terms_version->get<uint64_t>() > 0xffffffffull)
                R_STHROW(r_invalid_argument_exception, ("Plugin catalog entry \"%s\": terms_version isn't a whole number from 1.", e.id.c_str()));
            e.terms_version = (uint32_t)terms_version->get<uint64_t>();
            if(e.terms_url.empty())
                R_STHROW(r_invalid_argument_exception, ("Plugin catalog entry \"%s\" has terms_version but no terms_url.", e.id.c_str()));
        }

        if(e.type == r_catalog_entry_type::plugin)
        {
            e.file = str(p, "file");
            e.plugin = str(p, "plugin");
            e.manifest = str(p, "manifest");
            e.signature = str(p, "signature");
            for(const auto& name : {e.file, e.plugin, e.manifest, e.signature})
            {
                if(!_plain_file_name(name))
                    R_STHROW(r_invalid_argument_exception, ("Plugin catalog entry \"%s\": \"%s\" isn't a plain file name.", e.id.c_str(), name.c_str()));
            }
        }
        else
        {
            e.url = str(p, "url");
            if(!_https(e.url))
                R_STHROW(r_invalid_argument_exception, ("Plugin catalog entry \"%s\": url isn't an https url.", e.id.c_str()));
            for(const char* key : {"file", "plugin", "manifest", "signature"})
            {
                if(p.contains(key))
                    R_STHROW(r_invalid_argument_exception, ("Plugin catalog link entry \"%s\" has assets (%s).", e.id.c_str(), key));
            }
        }
        c.plugins.push_back(std::move(e));
    }
    return c;
}

r_plugin_check_result r_vss::check_plugin_catalog(const string& catalog_json,
                                                  const string& signature,
                                                  const string& revere_version,
                                                  const vector<string>& trusted_keys,
                                                  const string& platform,
                                                  const string& arch,
                                                  r_plugin_catalog& catalog)
{
    if(catalog_json.size() > MAX_CATALOG_BYTES)
        return _check(r_plugin_check::malformed, "the catalog is " + to_string(catalog_json.size()) + " bytes");
    if(signature.size() > MAX_SIGNATURE_BYTES)
        return _check(r_plugin_check::bad_signature, "the signature is " + to_string(signature.size()) + " bytes");

    // The signature first: nothing in an unsigned catalog is believed, or even parsed.
    bool signed_by_trusted = false;
    for(const auto& key : trusted_keys)
    {
        if(ecdsa_p256_sha256_verify(key,
                                    reinterpret_cast<const uint8_t*>(catalog_json.data()), catalog_json.size(),
                                    reinterpret_cast<const uint8_t*>(signature.data()), signature.size()))
        {
            signed_by_trusted = true;
            break;
        }
    }
    if(!signed_by_trusted)
        return _check(r_plugin_check::bad_signature,
                      trusted_keys.empty() ? string("no trusted keys") : string("no trusted key signed the catalog"));

    r_plugin_catalog c;
    try
    {
        c = parse_plugin_catalog(catalog_json);
    }
    catch(const exception& e)
    {
        return _check(r_plugin_check::malformed, e.what());
    }
    if(c.platform != platform || c.arch != arch)
        return _check(r_plugin_check::wrong_platform, "built for " + c.platform + " " + c.arch + ", this is " + platform + " " + arch);
    if(c.revere_version != revere_version)
        return _check(r_plugin_check::wrong_version, "for revere " + c.revere_version + ", this is " + revere_version);

    catalog = std::move(c);
    return _check(r_plugin_check::ok);
}

r_plugin_release_assets r_vss::catalog_release_assets(const string& revere_version, const string& platform, const string& arch)
{
    auto name = "revere-v" + revere_version + "-" + plugin_asset_platform(platform, arch) + "-plugins.json";
    r_plugin_release_assets a;
    a.manifest_url = RELEASE_DOWNLOADS + revere_version + "/" + name;
    a.signature_url = a.manifest_url + ".sig";
    return a;
}

r_plugin_release_assets r_vss::catalog_entry_assets(const string& release_version, const r_catalog_entry& entry)
{
    if(entry.type != r_catalog_entry_type::plugin)
        R_STHROW(r_invalid_argument_exception, ("Catalog entry \"%s\" isn't a plugin.", entry.id.c_str()));
    auto base = string(RELEASE_DOWNLOADS) + release_version + "/";
    r_plugin_release_assets a;
    a.plugin_url = base + entry.plugin;
    a.manifest_url = base + entry.manifest;
    a.signature_url = base + entry.signature;
    return a;
}

string r_vss::choose_catalog_release(const vector<r_published_release>& releases,
                                     const string& base_version,
                                     const string& platform,
                                     const string& arch)
{
    if(base_version.empty() || release_base_version(base_version) != base_version)
        return string();

    string best;
    for(const auto& r : releases)
    {
        if(compare_release_versions(r.version, base_version) > 0)
            continue;
        if(!best.empty() && compare_release_versions(r.version, best) <= 0)
            continue;
        auto a = catalog_release_assets(r.version, platform, arch);
        auto has = [&](const string& url) {
            return find(r.assets.begin(), r.assets.end(), url.substr(url.rfind('/') + 1)) != r.assets.end();
        };
        if(has(a.manifest_url) && has(a.signature_url))
            best = r.version;
    }
    return best;
}

r_catalog_fetch_result r_vss::fetch_plugin_catalog(const string& revere_version,
                                                   const r_plugin_fetch& fetch,
                                                   const vector<string>& trusted_keys,
                                                   const string& platform,
                                                   const string& arch)
{
    auto base = release_base_version(revere_version);
    if(base.empty())
        return _fetch_result(r_plugin_install::not_available, "this build's version (" + revere_version + ") names no release");

    string release = revere_version;   // a release build: its own
    if(base != revere_version)
    {
        // A dev build: the newest release at or below its base.
        vector<r_published_release> releases;
        try
        {
            auto body = fetch(REVERE_RELEASES_API_URL, MAX_RELEASES_BYTES);
            releases = parse_published_releases(string(body.begin(), body.end()));
        }
        catch(const exception& e)
        {
            return _fetch_result(r_plugin_install::download_failed, string("the list of releases: ") + e.what());
        }
        release = choose_catalog_release(releases, base, platform, arch);
        if(release.empty())
            return _fetch_result(r_plugin_install::not_available,
                                 "no release at or below v" + base + " has a plugin catalog for " + platform + " " + arch);
    }

    auto assets = catalog_release_assets(release, platform, arch);
    vector<uint8_t> catalog_bytes, signature_bytes;
    try
    {
        catalog_bytes = fetch(assets.manifest_url, MAX_CATALOG_BYTES);
    }
    catch(const r_not_found_exception&)
    {
        return _fetch_result(r_plugin_install::not_available, "release v" + release + " has no plugin catalog for " + platform + " " + arch);
    }
    catch(const exception& e)
    {
        return _fetch_result(r_plugin_install::download_failed, e.what());
    }
    try
    {
        signature_bytes = fetch(assets.signature_url, MAX_SIGNATURE_BYTES);
    }
    catch(const exception& e)
    {
        return _fetch_result(r_plugin_install::download_failed, e.what());
    }

    r_plugin_catalog catalog;
    auto check = check_plugin_catalog(string(catalog_bytes.begin(), catalog_bytes.end()),
                                      string(signature_bytes.begin(), signature_bytes.end()),
                                      release, trusted_keys, platform, arch, catalog);
    if(!check.ok())
        return _fetch_result(r_plugin_install::check_failed, string(plugin_check_name(check.status)) + ": " + check.detail, check.status);

    auto r = _fetch_result(r_plugin_install::ok, string(), r_plugin_check::ok);
    r.release_version = release;
    r.catalog = std::move(catalog);
    return r;
}

r_catalog_fetch_result r_vss::fetch_plugin_catalog()
{
    auto r = fetch_plugin_catalog(revere_version(),
                                  [](const string& url, size_t max_bytes) { return http_get(url, max_bytes, CATALOG_FETCH_TIMEOUT_MS); },
                                  trusted_plugin_keys(),
                                  plugin_platform(),
                                  plugin_arch());
    if(r.ok())
        R_LOG_INFO("Plugin catalog from release v%s lists %zu entries", r.release_version.c_str(), r.catalog.plugins.size());
    else
        R_LOG_INFO("No plugin catalog (%s): %s", plugin_install_name(r.status), r.detail.c_str());
    return r;
}

r_plugin_install_result r_vss::install_catalog_plugin(const r_catalog_entry& entry,
                                                      const string& release_version,
                                                      const string& dir,
                                                      const r_plugin_fetch& fetch,
                                                      const vector<string>& trusted_keys,
                                                      const string& platform,
                                                      const string& arch)
{
    // The plugin's manifest must be for exactly the release its catalog is.
    auto r = install_system_plugin(catalog_entry_assets(release_version, entry), dir, entry.file, fetch,
                                   release_version, trusted_keys, platform, arch);
    if(r.ok())
        r.release_version = release_version;
    return r;
}

r_plugin_install_result r_vss::install_catalog_plugin(const r_catalog_entry& entry,
                                                      const string& release_version,
                                                      const string& dir)
{
    auto r = install_catalog_plugin(entry, release_version, dir,
                                    [](const string& url, size_t max_bytes) { return http_get(url, max_bytes); },
                                    trusted_plugin_keys(), plugin_platform(), plugin_arch());
    if(r.ok())
        R_LOG_INFO("Installed plugin %s (%s), from release v%s", entry.id.c_str(), r.path.c_str(), r.release_version.c_str());
    else
        R_LOG_ERROR("Install of plugin %s failed (%s): %s", entry.id.c_str(), plugin_install_name(r.status), r.detail.c_str());
    return r;
}

const char* r_vss::plugin_state_name(r_plugin_state state)
{
    switch(state)
    {
        case r_plugin_state::not_installed: return "not_installed";
        case r_plugin_state::installed: return "installed";
        case r_plugin_state::needs_update: return "needs_update";
        case r_plugin_state::failed_check: return "failed_check";
        case r_plugin_state::removal_pending: return "removal_pending";
    }
    return "unknown";
}

r_plugin_state r_vss::plugin_entry_state(const r_catalog_entry& entry,
                                         const vector<string>& loaded,
                                         const vector<r_skipped_system_plugin>& skipped,
                                         const string& user_dir,
                                         string& detail)
{
    detail.clear();
    if(entry.type != r_catalog_entry_type::plugin)
        return r_plugin_state::not_installed;

    auto stem = fs::u8path(entry.file).stem().u8string();
    string name(stem.begin(), stem.end());

    if(!user_dir.empty())
    {
        auto path = (fs::u8path(user_dir) / fs::u8path(entry.file)).u8string();
        error_code ec;
        if(fs::exists(fs::u8path(plugin_uninstall_marker_path(string(path.begin(), path.end()))), ec))
            return r_plugin_state::removal_pending;
    }
    if(find(loaded.begin(), loaded.end(), name) != loaded.end())
        return r_plugin_state::installed;
    for(const auto& sk : skipped)
    {
        if(sk.name != name)
            continue;
        detail = string(plugin_check_name(sk.check.status)) + (sk.check.detail.empty() ? string() : ": " + sk.check.detail);
        return sk.needs_update() ? r_plugin_state::needs_update : r_plugin_state::failed_check;
    }
    return r_plugin_state::not_installed;
}

r_plugin_terms::r_plugin_terms(const string& path) :
    _path(path)
{
    try
    {
        if(path.empty() || !r_fs::file_exists(path))
            return;
        auto bytes = r_fs::read_file(path);
        auto j = nlohmann::json::parse(string(bytes.begin(), bytes.end()), nullptr, false);
        if(!j.is_object() || !j.contains("plugins") || !j["plugins"].is_object())
            return;
        for(auto it = j["plugins"].begin(); it != j["plugins"].end(); ++it)
        {
            const auto& v = it.value();
            if(!v.is_object() || !v.contains("terms_version") || !v["terms_version"].is_number_unsigned())
                continue;
            accepted a;
            a.id = it.key();
            a.version = (uint32_t)min<uint64_t>(v["terms_version"].get<uint64_t>(), 0xffffffffull);
            a.at = v.value("accepted_at", string());
            _accepted.push_back(std::move(a));
        }
    }
    catch(const exception& e)
    {
        R_LOG_WARNING("Could not read %s: %s", path.c_str(), e.what());
        _accepted.clear();
    }
}

uint32_t r_plugin_terms::accepted_version(const string& id) const
{
    lock_guard<mutex> g(_lock);
    for(const auto& a : _accepted)
    {
        if(a.id == id)
            return a.version;
    }
    return 0;
}

bool r_plugin_terms::needs_acceptance(const r_catalog_entry& entry) const
{
    return entry.terms_version > 0 && accepted_version(entry.id) < entry.terms_version;
}

bool r_plugin_terms::accept(const string& id, uint32_t terms_version, string& why)
{
    lock_guard<mutex> g(_lock);
    auto next = _accepted;
    auto it = find_if(next.begin(), next.end(), [&](const accepted& a) { return a.id == id; });
    if(it != next.end() && it->version >= terms_version)
        return true;   // already accepted, this version or a later one
    if(it == next.end())
    {
        next.push_back({id, terms_version, _utc_now()});
    }
    else
    {
        it->version = terms_version;
        it->at = _utc_now();
    }

    nlohmann::json plugins = nlohmann::json::object();
    for(const auto& a : next)
        plugins[a.id] = {{"terms_version", a.version}, {"accepted_at", a.at}};
    auto text = nlohmann::json({{"plugins", plugins}}).dump(2) + "\n";
    try
    {
        r_fs::atomic_write_file(reinterpret_cast<const uint8_t*>(text.data()), text.size(), _path);
    }
    catch(const exception& e)
    {
        why = string("could not record the terms acceptance: ") + e.what();
        return false;
    }
    _accepted = std::move(next);
    R_LOG_INFO("Plugin %s terms version %u accepted", id.c_str(), (unsigned)terms_version);
    return true;
}
