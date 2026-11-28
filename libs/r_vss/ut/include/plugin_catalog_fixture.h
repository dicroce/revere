#ifndef _plugin_catalog_fixture_h
#define _plugin_catalog_fixture_h

#include <string>

// Plugin catalogs, and a second plugin's manifest, signed with OpenSSL the way the release
// CI signs (see plugin_manifest_fixture.h), by CATALOG_KEY, a key made for these tests whose
// private half was thrown away. For test_r_plugin_catalog.
namespace catalog_fixture
{

inline const std::string CATALOG_KEY =
    "-----BEGIN PUBLIC KEY-----\n"
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEOvXZH+B4hhdCLH+LicDBmCMrhywt\n"
    "eF3HCnmJ4nAw8D4N1rbNNa8N+v6VmU5TnoV8I2Nwm9rF5RSq/9rlkX6vig==\n"
    "-----END PUBLIC KEY-----\n";

// windows x86_64, revere 0.0.41: Revere Cloud (terms version 2) and "second" (beta, open
// source) as plugin entries, and HomeKit as a link.
// The catalog.
inline const std::string CATALOG =
    "{\"format\":1,\"platform\":\"windows\",\"arch\":\"x86_64\",\"revere_version\":\"0.0.41\",\"plugins\":[{\"id\":\"revere_cloud\",\"type\":\"plugin\",\"name\":\"Revere Cloud\",\"description\":\"Cloud sync and remote access.\",\"source\":\"closed\",\"terms_url\":\"https://example.test/terms\",\"terms_version\":2,\"file\":\"test_system_plugin.dll\",\"plugin\":\"revere-v0.0.41-windows-cloud-plugin.dll\",\"manifest\":\"revere-v0.0.41-windows-cloud-plugin.dll.json\",\"signature\":\"revere-v0.0.41-windows-cloud-plugin.dll.json.sig\"},{\"id\":\"second\",\"type\":\"plugin\",\"name\":\"Second\",\"description\":\"A second plugin.\",\"source\":\"open\",\"beta\":true,\"file\":\"second_system_plugin.dll\",\"plugin\":\"revere-v0.0.41-windows-second-plugin.dll\",\"manifest\":\"revere-v0.0.41-windows-second-plugin.dll.json\",\"signature\":\"revere-v0.0.41-windows-second-plugin.dll.json.sig\"},{\"id\":\"homekit\",\"type\":\"link\",\"name\":\"HomeKit\",\"description\":\"Apple Home.\",\"source\":\"open\",\"beta\":true,\"url\":\"https://example.test/homekit\"}]}";
inline const std::string CATALOG_SIG_HEX =
    "304502207a010859176f4e50b9fd5a16a7b913162bdb7264278be78612fd26f72c5b7441022100ed4c8c5da5bea12f6d56f42ab95631c8910ffd2bb89d0d19cf88d6cded809a26";

// Signed, but format 2.
inline const std::string CATALOG_FORMAT2 =
    "{\"format\":2,\"platform\":\"windows\",\"arch\":\"x86_64\",\"revere_version\":\"0.0.41\",\"plugins\":[]}";
inline const std::string CATALOG_FORMAT2_SIG_HEX =
    "304402205a153b928936e389949098b3e7bfcbb6e91e2bf244f2539c012576b89e6c0923022046539a4598bdac1b0efba0429af61a6a51075dfd4c0dab826b338517fb752e58";

// Signed, but for linux.
inline const std::string CATALOG_LINUX =
    "{\"format\":1,\"platform\":\"linux\",\"arch\":\"x86_64\",\"revere_version\":\"0.0.41\",\"plugins\":[]}";
inline const std::string CATALOG_LINUX_SIG_HEX =
    "304402206030bc7653781f75c7481f8af0ae89a77851342e1e33a076b3aa0a24257ce34502206bf163e2b43beef758cc19f664d57ed24f29b0088b3a793217aec55aa1a08b43";

// Signed, but for revere 0.0.42.
inline const std::string CATALOG_V42 =
    "{\"format\":1,\"platform\":\"windows\",\"arch\":\"x86_64\",\"revere_version\":\"0.0.42\",\"plugins\":[]}";
inline const std::string CATALOG_V42_SIG_HEX =
    "304502201209bcddfd1346a28e59dc572367757ef9933d8e890493d2be87192f7f6fa7f9022100c5c9f3404cd53a81b60addea644c75c4ecab386f6a91f742957149771db86e7b";

// Signed, but with an id twice.
inline const std::string CATALOG_DUP_ID =
    "{\"format\":1,\"platform\":\"windows\",\"arch\":\"x86_64\",\"revere_version\":\"0.0.41\",\"plugins\":[{\"id\":\"homekit\",\"type\":\"link\",\"name\":\"HomeKit\",\"description\":\"Apple Home.\",\"source\":\"open\",\"beta\":true,\"url\":\"https://example.test/homekit\"},{\"id\":\"homekit\",\"type\":\"link\",\"name\":\"HomeKit\",\"description\":\"Apple Home.\",\"source\":\"open\",\"beta\":true,\"url\":\"https://example.test/homekit\"}]}";
inline const std::string CATALOG_DUP_ID_SIG_HEX =
    "30450221008c0b4206839091d39a7d2667b15b80d86808d4b8729c5d5db1bd67dd7d83c85702206700680f4c6e796697445d1b798a19664038d96ec97deccc672f1ba3687bd986";

// Signed, but an asset name has a folder in it.
inline const std::string CATALOG_FOLDER =
    "{\"format\":1,\"platform\":\"windows\",\"arch\":\"x86_64\",\"revere_version\":\"0.0.41\",\"plugins\":[{\"id\":\"revere_cloud\",\"type\":\"plugin\",\"name\":\"Revere Cloud\",\"description\":\"Cloud sync and remote access.\",\"source\":\"closed\",\"terms_url\":\"https://example.test/terms\",\"terms_version\":2,\"file\":\"test_system_plugin.dll\",\"plugin\":\"../evil.dll\",\"manifest\":\"revere-v0.0.41-windows-cloud-plugin.dll.json\",\"signature\":\"revere-v0.0.41-windows-cloud-plugin.dll.json.sig\"}]}";
inline const std::string CATALOG_FOLDER_SIG_HEX =
    "3045022026d3e730f7e898f94031fed799bd0766a273b619109002bfed90b32f9c555cbd022100ac4a971ffb268724b52f16fa25be82a253b24aa4be0b8a095cb9288890662db3";

// The manifest of the "second" plugin (second_system_plugin.dll, SECOND_BYTES).
inline const std::string SECOND_MANIFEST =
    "{\"format\":1,\"platform\":\"windows\",\"arch\":\"x86_64\",\"revere_version\":\"0.0.41\",\"file\":\"second_system_plugin.dll\",\"size\":14,\"sha256\":\"a3c17406e006d09acc4f8f2e3f6bae030592ba8e6f1a62c833aa5ef89b537b9c\"}";
inline const std::string SECOND_MANIFEST_SIG_HEX =
    "3046022100961d56b93d97a1705beed797d2cf2ccf3d44efbe6e41fb848b289581d36ad399022100b393e1d0e34af14b19a1fb38f7e78cdba09c4f2ce662bac143e844e8e6ad99cf";

inline const std::string SECOND_BYTES = "second plugin\n";

}

#endif
