
#include "r_vss/r_plugin_manifest.h"
#include "r_vss/r_build_info.h"
#include "r_utils/r_ecdsa.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_sha256.h"
#include "r_utils/3rdparty/json/json.h"
#include <filesystem>
#include <fstream>

using namespace r_vss;
using namespace r_utils;
using namespace std;
namespace fs = std::filesystem;

namespace
{

// Far more than a manifest or a DER P-256 signature (at most 72 bytes) needs:
// anything bigger isn't one, and isn't read.
constexpr uintmax_t MAX_MANIFEST_BYTES = 64 * 1024;
constexpr uintmax_t MAX_SIGNATURE_BYTES = 1024;

r_plugin_check_result _result(r_plugin_check status, const string& detail = string())
{
    r_plugin_check_result r;
    r.status = status;
    r.detail = detail;
    return r;
}

// The whole file, if it's no bigger than max_bytes.
bool _read_small(const string& path, uintmax_t max_bytes, string& out, string& why)
{
    error_code ec;
    auto size = fs::file_size(fs::u8path(path), ec);
    if(ec)
    {
        why = path + ": " + ec.message();
        return false;
    }
    if(size > max_bytes)
    {
        why = path + " is " + to_string(size) + " bytes, more than " + to_string(max_bytes);
        return false;
    }
    ifstream f(fs::u8path(path), ios::binary);
    if(!f)
    {
        why = path + ": can't open it";
        return false;
    }
    out.assign(istreambuf_iterator<char>(f), istreambuf_iterator<char>());
    if(f.bad())
    {
        why = path + ": read failed";
        return false;
    }
    return true;
}

bool _is_lower_hex_sha256(const string& s)
{
    if(s.size() != 64)
        return false;
    for(char c : s)
    {
        if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    }
    return true;
}

}

r_plugin_manifest r_vss::parse_plugin_manifest(const string& json)
{
    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(json);
    }
    catch(const exception& e)
    {
        R_STHROW(r_invalid_argument_exception, ("Plugin manifest isn't json: %s", e.what()));
    }
    if(!j.is_object())
        R_STHROW(r_invalid_argument_exception, ("Plugin manifest isn't a json object."));

    auto str = [&](const char* key) {
        auto it = j.find(key);
        if(it == j.end() || !it->is_string())
            R_STHROW(r_invalid_argument_exception, ("Plugin manifest has no string \"%s\".", key));
        return it->get<string>();
    };
    auto unsigned_number = [&](const char* key) {
        auto it = j.find(key);
        if(it == j.end() || !it->is_number_unsigned())
            R_STHROW(r_invalid_argument_exception, ("Plugin manifest has no whole number \"%s\".", key));
        return it->get<uint64_t>();
    };

    r_plugin_manifest m;
    auto format = unsigned_number("format");
    if(format != 1)
        R_STHROW(r_invalid_argument_exception, ("Plugin manifest format %llu isn't one this revere reads (1).", (unsigned long long)format));
    m.format = 1;
    m.platform = str("platform");
    m.arch = str("arch");
    m.revere_version = str("revere_version");
    m.file = str("file");
    m.size = unsigned_number("size");
    m.sha256 = str("sha256");

    if(m.file.empty() || m.file.find_first_of("/\\:") != string::npos || m.file == "." || m.file == "..")
        R_STHROW(r_invalid_argument_exception, ("Plugin manifest's file \"%s\" isn't a plain file name.", m.file.c_str()));
    if(!_is_lower_hex_sha256(m.sha256))
        R_STHROW(r_invalid_argument_exception, ("Plugin manifest's sha256 isn't 64 lowercase hex digits."));
    return m;
}

const char* r_vss::plugin_check_name(r_plugin_check status)
{
    switch(status)
    {
        case r_plugin_check::ok: return "ok";
        case r_plugin_check::no_manifest: return "no_manifest";
        case r_plugin_check::bad_signature: return "bad_signature";
        case r_plugin_check::malformed: return "malformed";
        case r_plugin_check::wrong_file: return "wrong_file";
        case r_plugin_check::wrong_platform: return "wrong_platform";
        case r_plugin_check::wrong_version: return "wrong_version";
        case r_plugin_check::size_mismatch: return "size_mismatch";
        case r_plugin_check::hash_mismatch: return "hash_mismatch";
        case r_plugin_check::unreadable: return "unreadable";
    }
    return "unknown";
}

string r_vss::plugin_manifest_path(const string& plugin_path)
{
    return plugin_path + ".json";
}

string r_vss::plugin_signature_path(const string& plugin_path)
{
    return plugin_path + ".json.sig";
}

string r_vss::plugin_platform()
{
#if defined(IS_WINDOWS)
    return "windows";
#elif defined(IS_MACOS)
    return "macos";
#else
    return "linux";
#endif
}

string r_vss::plugin_arch()
{
#if defined(_M_X64) || defined(__x86_64__)
    return "x86_64";
#elif defined(_M_ARM64) || defined(__aarch64__)
    return "arm64";
#else
    return "unknown";
#endif
}

const vector<string>& r_vss::trusted_plugin_keys()
{
    // The release signing key's public half (P-256). Its private half is the
    // REVERE_PLUGIN_SIGNING_KEY secret in dicroce/revere's release environment, and
    // Scott's encrypted offline copy. Generated 2026-11-12.
    static const vector<string> keys = {
        "-----BEGIN PUBLIC KEY-----\n"
        "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEyoJaveiI9dV8iMd1BCuTJ5waLQlE\n"
        "Lb/o4EjKncI5WScnbgqhP/t4XoNJxwdCaXcnMAEmMUy733L4laq3PEZGhg==\n"
        "-----END PUBLIC KEY-----\n"
    };
    return keys;
}

r_plugin_check_result r_vss::check_system_plugin(const string& plugin_path,
                                                 const string& revere_version,
                                                 const vector<string>& trusted_keys,
                                                 const string& platform,
                                                 const string& arch)
{
    auto manifest_path = plugin_manifest_path(plugin_path);
    auto signature_path = plugin_signature_path(plugin_path);

    error_code ec;
    if(!fs::exists(fs::u8path(manifest_path), ec))
        return _result(r_plugin_check::no_manifest, "no " + manifest_path);
    if(!fs::exists(fs::u8path(signature_path), ec))
        return _result(r_plugin_check::bad_signature, "no " + signature_path);

    string manifest_json, signature, why;
    if(!_read_small(manifest_path, MAX_MANIFEST_BYTES, manifest_json, why))
        return _result(r_plugin_check::unreadable, why);
    if(!_read_small(signature_path, MAX_SIGNATURE_BYTES, signature, why))
        return _result(r_plugin_check::bad_signature, why);

    // The signature first: nothing in an unsigned manifest is believed, or even parsed.
    bool signed_by_trusted = false;
    for(const auto& key : trusted_keys)
    {
        if(ecdsa_p256_sha256_verify(key,
                                    reinterpret_cast<const uint8_t*>(manifest_json.data()), manifest_json.size(),
                                    reinterpret_cast<const uint8_t*>(signature.data()), signature.size()))
        {
            signed_by_trusted = true;
            break;
        }
    }
    if(!signed_by_trusted)
        return _result(r_plugin_check::bad_signature,
                       trusted_keys.empty() ? string("no trusted keys") : "no trusted key signed " + manifest_path);

    r_plugin_manifest m;
    try
    {
        m = parse_plugin_manifest(manifest_json);
    }
    catch(const exception& e)
    {
        return _result(r_plugin_check::malformed, e.what());
    }

    auto file_name = fs::u8path(plugin_path).filename().u8string();
    if(m.file != string(file_name.begin(), file_name.end()))
        return _result(r_plugin_check::wrong_file, "the manifest is for " + m.file);
    if(m.platform != platform || m.arch != arch)
        return _result(r_plugin_check::wrong_platform, "built for " + m.platform + " " + m.arch + ", this is " + platform + " " + arch);
    if(m.revere_version != revere_version)
        return _result(r_plugin_check::wrong_version, "built for revere " + m.revere_version + ", this is " + revere_version);

    auto size = fs::file_size(fs::u8path(plugin_path), ec);
    if(ec)
        return _result(r_plugin_check::unreadable, plugin_path + ": " + ec.message());
    if(size != m.size)
        return _result(r_plugin_check::size_mismatch, "it's " + to_string(size) + " bytes, the manifest says " + to_string(m.size));

    ifstream f(fs::u8path(plugin_path), ios::binary);
    if(!f)
        return _result(r_plugin_check::unreadable, plugin_path + ": can't open it");
    r_sha256 hash;
    vector<char> buffer(64 * 1024);
    while(f)
    {
        f.read(buffer.data(), (streamsize)buffer.size());
        auto got = f.gcount();
        if(got > 0)
            hash.update(reinterpret_cast<const uint8_t*>(buffer.data()), (size_t)got);
    }
    if(f.bad())
        return _result(r_plugin_check::unreadable, plugin_path + ": read failed");
    hash.finalize();
    if(hash.get_as_string() != m.sha256)
        return _result(r_plugin_check::hash_mismatch, "its sha256 isn't the manifest's");

    return _result(r_plugin_check::ok);
}

r_plugin_check_result r_vss::check_system_plugin(const string& plugin_path)
{
    return check_system_plugin(plugin_path, revere_version(), trusted_plugin_keys(), plugin_platform(), plugin_arch());
}
