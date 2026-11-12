#ifndef _plugin_manifest_fixture_h
#define _plugin_manifest_fixture_h

#include <string>

// A signed plugin manifest made with OpenSSL, the way the release CI signs:
//   openssl ecparam -name prime256v1 -genkey -noout -out key.pem
//   openssl ec -in key.pem -pubout -out pub.pem
//   openssl dgst -sha256 -sign key.pem -out manifest.json.sig manifest.json
// Only the public keys and the signatures are here; the private keys were thrown away.
// For test_r_plugin_manifest and test_r_plugin_installer.
namespace plugin_fixture
{

inline const std::string TEST_KEY =
    "-----BEGIN PUBLIC KEY-----\n"
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE6I2Lwyx1+Q0pr6GIMBhOCuJTwR/9\n"
    "Zg3mkyYoWDi4oFtEMELLeZUGHHf4+yvdBf//K0hT4TQ00P9oi/LOAQYM0w==\n"
    "-----END PUBLIC KEY-----\n";

// Another P-256 key, which signed nothing here.
inline const std::string OTHER_KEY =
    "-----BEGIN PUBLIC KEY-----\n"
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEajuYUQhNvyK680+i7tAnNHVDpt6g\n"
    "uYbQXzLmQtgybfNuwuNItRb9bmmFoOIyhxathmD6Ab74gyVX8hPstXIiuw==\n"
    "-----END PUBLIC KEY-----\n";

// The plugin file the manifest describes (19 bytes).
inline const std::string PLUGIN_BYTES = "revere test plugin\n";

inline const std::string MANIFEST =
    "{\"format\":1,\"platform\":\"windows\",\"arch\":\"x86_64\",\"revere_version\":\"0.0.41\","
    "\"file\":\"test_system_plugin.dll\",\"size\":19,"
    "\"sha256\":\"20616576e59d83e78286ed236fe12429c6830f0e851b8e69c882a0f637f5755e\"}";

inline const std::string MANIFEST_SIG_HEX =
    "304502201fc91886f8ed07cf168ea99996f6f291ccba5a1aeeb493e21d56d98a27e5dad3"
    "022100d5d82cfc03b0a6977cb28135fe3445ec7e981a1904fe6424edc270d3ad344361";

// Signed by TEST_KEY too, but not a manifest.
inline const std::string NOT_A_MANIFEST = "not a manifest";
inline const std::string NOT_A_MANIFEST_SIG_HEX =
    "3046022100d9b1c6cd98ad414254590fdfab44680d53c66eedd261beecd9141f916ebf28b1"
    "022100a049fbcb445b226a795da8f83748866fa6347a64528853ebd992fa04c67ff4a5";

inline std::string unhex(const std::string& hex)
{
    std::string out;
    for(size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back((char)std::stoi(hex.substr(i, 2), nullptr, 16));
    return out;
}

}

#endif
