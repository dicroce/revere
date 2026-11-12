
#include "r_vss/r_plugin_installer.h"
#include "r_vss/r_build_info.h"
#include "r_http/r_client_response.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_sha256.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_ssl_socket.h"
#include "r_utils/r_string_utils.h"
#include <mbedtls/ssl.h>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <memory>

using namespace r_vss;
using namespace r_utils;
using namespace std;
namespace fs = std::filesystem;

namespace
{

constexpr int MAX_REDIRECTS = 5;
constexpr size_t MAX_MANIFEST_BYTES = 64 * 1024;
constexpr size_t MAX_SIGNATURE_BYTES = 1024;

string _lower(string s)
{
    for(auto& c : s)
        c = (char)tolower((unsigned char)c);
    return s;
}

// r_ssl_socket::recv gives up with MBEDTLS_ERR_SSL_WANT_READ after 100 ms without
// application data, and r_client_response takes that for a closed connection (or,
// in a body, for its end). A TLS 1.3 server's session tickets (GitHub's) make it
// common: the socket turns readable, the ticket is consumed, and the response is
// still on its way. This retries those until the request's own deadline. (Fixing
// r_ssl_socket itself would change every revere_cloud connection's timing.)
class patient_socket : public r_socket_base
{
public:
    patient_socket(unique_ptr<r_socket_base> inner, uint64_t timeout_ms) :
        _inner(std::move(inner)),
        _timeout(chrono::milliseconds(timeout_ms))
    {
    }

    void connect(const string& host, int port) override { _inner->connect(host, port); }
    void close() override { _inner->close(); }
    bool valid() const override { return _inner->valid(); }
    int send(const void* buf, size_t len) override { return _inner->send(buf, len); }
    bool wait_till_recv_wont_block(uint64_t& millis) const override { return _inner->wait_till_recv_wont_block(millis); }
    bool wait_till_send_wont_block(uint64_t& millis) const override { return _inner->wait_till_send_wont_block(millis); }
    bool peer_closed() const override { return _inner->peer_closed(); }
    string get_peer_ip() const override { return _inner->get_peer_ip(); }

    int recv(void* buf, size_t len) override
    {
        auto deadline = chrono::steady_clock::now() + _timeout;
        while(true)
        {
            int r = _inner->recv(buf, len);
            bool again = (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE);
            if(!again || !_inner->valid() || chrono::steady_clock::now() >= deadline)
                return r;
        }
    }

private:
    unique_ptr<r_socket_base> _inner;
    chrono::milliseconds _timeout;
};

// A TLS socket that checks the server's certificate when this machine has root
// certificates to check it with, and doesn't when it hasn't (r_ssl_socket throws
// then). The plugin's signature is what's trusted either way.
unique_ptr<r_socket_base> _tls_socket()
{
    try
    {
        return make_unique<r_ssl_socket>(true);
    }
    catch(const exception& e)
    {
        R_LOG_WARNING("No root certificates to check the plugin download's server with (%s); relying on its signature alone.", e.what());
        return make_unique<r_ssl_socket>(false);
    }
}

r_plugin_install_result _result(r_plugin_install status, const string& detail, r_plugin_check check = r_plugin_check::unreadable)
{
    r_plugin_install_result r;
    r.status = status;
    r.check = check;
    r.detail = detail;
    return r;
}

string _plugin_ext(const string& platform)
{
    if(platform == "windows")
        return ".dll";
    if(platform == "macos")
        return ".dylib";
    return ".so";
}

}

r_http_url r_vss::parse_http_url(const string& url)
{
    r_http_url u;
    auto scheme_end = url.find("://");
    if(scheme_end == string::npos)
        R_STHROW(r_invalid_argument_exception, ("Not an absolute URL: %s", url.c_str()));
    u.scheme = _lower(url.substr(0, scheme_end));
    if(u.scheme != "https" && u.scheme != "http")
        R_STHROW(r_invalid_argument_exception, ("Not an http or https URL: %s", url.c_str()));

    auto authority_start = scheme_end + 3;
    auto target_start = url.find_first_of("/?#", authority_start);
    auto authority = url.substr(authority_start, target_start == string::npos ? string::npos : target_start - authority_start);
    if(authority.empty() || authority.find('@') != string::npos)
        R_STHROW(r_invalid_argument_exception, ("No host, or a user name, in URL: %s", url.c_str()));

    u.port = (u.scheme == "https") ? 443 : 80;
    auto colon = authority.rfind(':');
    if(colon != string::npos && authority.find(']') == string::npos)
    {
        auto port = authority.substr(colon + 1);
        if(port.empty() || port.size() > 5 || port.find_first_not_of("0123456789") != string::npos)
            R_STHROW(r_invalid_argument_exception, ("Bad port in URL: %s", url.c_str()));
        u.port = stoi(port);
        if(u.port < 1 || u.port > 65535)
            R_STHROW(r_invalid_argument_exception, ("Bad port in URL: %s", url.c_str()));
        authority = authority.substr(0, colon);
    }
    if(authority.empty())
        R_STHROW(r_invalid_argument_exception, ("No host in URL: %s", url.c_str()));
    u.host = authority;

    if(target_start == string::npos)
        u.target = "/";
    else
    {
        u.target = url.substr(target_start);
        auto hash = u.target.find('#');   // never sent
        if(hash != string::npos)
            u.target = u.target.substr(0, hash);
        if(u.target.empty() || u.target[0] != '/')
            u.target = "/" + u.target;
    }
    for(char c : u.target)
    {
        if((unsigned char)c <= 0x20 || c == 0x7f)
            R_STHROW(r_invalid_argument_exception, ("A space or control character in URL: %s", url.c_str()));
    }
    return u;
}

string r_vss::resolve_redirect(const string& from_url, const string& location)
{
    if(location.find("://") != string::npos)
    {
        parse_http_url(location);   // throws if it isn't one
        return location;
    }
    auto from = parse_http_url(from_url);
    if(location.size() >= 2 && location[0] == '/' && location[1] == '/')
        return from.scheme + ":" + location;
    if(!location.empty() && location[0] == '/')
    {
        bool default_port = (from.scheme == "https" && from.port == 443) || (from.scheme == "http" && from.port == 80);
        return from.scheme + "://" + from.host + (default_port ? string() : ":" + to_string(from.port)) + location;
    }
    R_STHROW(r_invalid_argument_exception, ("A redirect to a relative path isn't followed: %s", location.c_str()));
}

vector<uint8_t> r_vss::http_get(const string& url, size_t max_bytes, uint64_t timeout_ms)
{
    string current = url;
    bool was_https = false;
    for(int hop = 0; hop <= MAX_REDIRECTS; ++hop)
    {
        auto u = parse_http_url(current);
        if(was_https && u.scheme != "https")
            R_STHROW(r_exception, ("Refusing a redirect from https to %s", current.c_str()));
        was_https = was_https || u.scheme == "https";

        unique_ptr<r_socket_base> raw;
        if(u.scheme == "https")
            raw = _tls_socket();
        else
            raw = make_unique<r_socket>();
        auto sok = make_unique<patient_socket>(std::move(raw), timeout_ms);
        sok->connect(u.host, u.port);

        // Written here, not by r_client_request: r_uri re-encodes and reorders a query,
        // which breaks a signed CDN URL.
        bool default_port = (u.scheme == "https" && u.port == 443) || (u.scheme == "http" && u.port == 80);
        string request = "GET " + u.target + " HTTP/1.1\r\n"
                         "Host: " + u.host + (default_port ? string() : ":" + to_string(u.port)) + "\r\n"
                         "User-Agent: revere/" + revere_version() + "\r\n"
                         "Accept: */*\r\n"
                         "Connection: close\r\n"
                         "\r\n";
        r_networking::r_send(*sok, request.data(), request.size(), timeout_ms);

        r_http::r_client_response response;
        response.read_response(*sok, timeout_ms);
        int status = response.get_status();

        if(status == 301 || status == 302 || status == 303 || status == 307 || status == 308)
        {
            auto location = response.get_header("location");
            if(location.empty())
                R_STHROW(r_exception, ("A %d redirect with no Location from %s", status, u.host.c_str()));
            current = resolve_redirect(current, location);
            continue;
        }
        if(status == 404)
            R_STHROW(r_not_found_exception, ("Not found: %s", url.c_str()));
        if(status < 200 || status > 299)
            R_STHROW(r_exception, ("HTTP %d from %s", status, u.host.c_str()));
        if(response.get_body_size() > max_bytes)
            R_STHROW(r_exception, ("%s is %zu bytes, more than %zu", url.c_str(), response.get_body_size(), max_bytes));
        auto content_length = response.get_header("content-length");
        if(!content_length.empty() && content_length.find_first_not_of("0123456789") == string::npos &&
           response.get_body_size() != (size_t)stoull(content_length))
            R_STHROW(r_exception, ("The download of %s was cut short: %zu of %s bytes", url.c_str(), response.get_body_size(), content_length.c_str()));
        return response.release_body();
    }
    R_STHROW(r_exception, ("More than %d redirects from %s", MAX_REDIRECTS, url.c_str()));
}

string r_vss::revere_cloud_plugin_file()
{
    return "revere_cloud_system_plugin" + _plugin_ext(plugin_platform());
}

r_plugin_release_assets r_vss::revere_cloud_release_assets(const string& revere_version, const string& platform, const string& arch)
{
    // The installers' platform names: revere-v0.0.41-linux-arm64-cloud.run, ...
    string asset_platform = platform;
    if(platform == "linux" && arch == "arm64")
        asset_platform = "linux-arm64";

    auto base = "https://github.com/dicroce/revere/releases/download/v" + revere_version + "/";
    auto plugin = "revere-v" + revere_version + "-" + asset_platform + "-cloud-plugin" + _plugin_ext(platform);

    r_plugin_release_assets a;
    a.plugin_url = base + plugin;
    a.manifest_url = base + plugin_manifest_path(plugin);
    a.signature_url = base + plugin_signature_path(plugin);
    return a;
}

const char* r_vss::plugin_install_name(r_plugin_install status)
{
    switch(status)
    {
        case r_plugin_install::ok: return "ok";
        case r_plugin_install::not_available: return "not_available";
        case r_plugin_install::download_failed: return "download_failed";
        case r_plugin_install::check_failed: return "check_failed";
        case r_plugin_install::write_failed: return "write_failed";
    }
    return "unknown";
}

r_plugin_install_result r_vss::install_system_plugin(const r_plugin_release_assets& assets,
                                                     const string& dir,
                                                     const string& file_name,
                                                     const r_plugin_fetch& fetch,
                                                     const string& revere_version,
                                                     const vector<string>& trusted_keys,
                                                     const string& platform,
                                                     const string& arch)
{
    // 1. The manifest and its signature, checked before the plugin is fetched.
    vector<uint8_t> manifest_bytes, signature_bytes;
    try
    {
        manifest_bytes = fetch(assets.manifest_url, MAX_MANIFEST_BYTES);
    }
    catch(const r_not_found_exception&)
    {
        return _result(r_plugin_install::not_available, "this release has no plugin for " + platform + " " + arch);
    }
    catch(const exception& e)
    {
        return _result(r_plugin_install::download_failed, e.what());
    }
    try
    {
        signature_bytes = fetch(assets.signature_url, MAX_SIGNATURE_BYTES);
    }
    catch(const exception& e)
    {
        return _result(r_plugin_install::download_failed, e.what());
    }

    string manifest_json(manifest_bytes.begin(), manifest_bytes.end());
    string signature(signature_bytes.begin(), signature_bytes.end());
    r_plugin_manifest m;
    auto check = check_plugin_manifest(manifest_json, signature, file_name, revere_version, trusted_keys, platform, arch, m);
    if(!check.ok())
        return _result(r_plugin_install::check_failed, string(plugin_check_name(check.status)) + ": " + check.detail, check.status);

    // 2. The plugin, checked against the manifest.
    vector<uint8_t> plugin;
    try
    {
        plugin = fetch(assets.plugin_url, (size_t)m.size);
    }
    catch(const exception& e)
    {
        return _result(r_plugin_install::download_failed, e.what());
    }
    if(plugin.size() != m.size)
        return _result(r_plugin_install::check_failed,
                       "size_mismatch: got " + to_string(plugin.size()) + " bytes, the manifest says " + to_string(m.size),
                       r_plugin_check::size_mismatch);
    r_sha256 hash;
    hash.update(plugin.data(), plugin.size());
    hash.finalize();
    if(hash.get_as_string() != m.sha256)
        return _result(r_plugin_install::check_failed, "hash_mismatch: its sha256 isn't the manifest's", r_plugin_check::hash_mismatch);

    // 3. Written beside any old copy: signature, manifest, then the plugin, each atomically.
    auto path = (fs::u8path(dir) / fs::u8path(file_name)).u8string();
    string plugin_path(path.begin(), path.end());
    try
    {
        r_fs::mkdir_p(dir);
        // Temporary files an interrupted install left behind.
        error_code ec;
        for(fs::directory_iterator it(fs::u8path(dir), ec), end; !ec && it != end; it.increment(ec))
        {
            auto name = it->path().filename().u8string();
            if(r_string_utils::starts_with(string(name.begin(), name.end()), file_name) &&
               string(name.begin(), name.end()).find(".tmp-") != string::npos)
            {
                error_code rec;
                fs::remove(it->path(), rec);
            }
        }
        r_fs::atomic_write_file(signature_bytes.data(), signature_bytes.size(), plugin_signature_path(plugin_path));
        r_fs::atomic_write_file(manifest_bytes.data(), manifest_bytes.size(), plugin_manifest_path(plugin_path));
        r_fs::atomic_write_file(plugin.data(), plugin.size(), plugin_path);
    }
    catch(const exception& e)
    {
        return _result(r_plugin_install::write_failed, e.what());
    }

    // 4. As the loader will see it.
    auto installed = check_system_plugin(plugin_path, revere_version, trusted_keys, platform, arch);
    if(!installed.ok())
        return _result(r_plugin_install::check_failed,
                       string("after writing it, ") + plugin_check_name(installed.status) + ": " + installed.detail, installed.status);

    auto r = _result(r_plugin_install::ok, string(), r_plugin_check::ok);
    r.path = plugin_path;
    return r;
}

r_plugin_install_result r_vss::install_revere_cloud_plugin(const string& dir)
{
    auto platform = plugin_platform();
    auto arch = plugin_arch();
    auto r = install_system_plugin(revere_cloud_release_assets(revere_version(), platform, arch),
                                   dir,
                                   revere_cloud_plugin_file(),
                                   [](const string& url, size_t max_bytes) { return http_get(url, max_bytes); },
                                   revere_version(),
                                   trusted_plugin_keys(),
                                   platform,
                                   arch);
    if(r.ok())
        R_LOG_INFO("Installed Revere Cloud plugin %s", r.path.c_str());
    else
        R_LOG_ERROR("Revere Cloud plugin install failed (%s): %s", plugin_install_name(r.status), r.detail.c_str());
    return r;
}
