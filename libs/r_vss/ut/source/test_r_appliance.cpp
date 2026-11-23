#include "test_r_appliance.h"
#include "r_vss/r_appliance.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"
#include <filesystem>
#include <mutex>
#include <random>
#include <string>
#include <vector>

using namespace r_vss;
using namespace r_utils;
using namespace std;
namespace fs = std::filesystem;

REGISTER_TEST_FIXTURE(test_r_appliance);

using verdict = r_appliance::set_password_verdict;
using auth = r_appliance::set_password_auth;

static string s_top;

static void _write(const string& contents)
{
    r_fs::write_file((const uint8_t*)contents.data(), contents.size(), r_appliance::path(s_top));
}

void test_r_appliance::setup()
{
    s_top = (fs::temp_directory_path() / ("r_appliance_ut_" + to_string(random_device()()))).string();
    fs::create_directories(s_top);
}

void test_r_appliance::teardown()
{
    r_logger::clear_log_callback();
    error_code ec;
    fs::remove_all(s_top, ec);
}

void test_r_appliance::test_read_flag_missing_file()
{
    RTF_ASSERT(r_appliance::path(s_top) == s_top + PATH_SLASH + "appliance.json");
    RTF_ASSERT(!r_appliance::read_flag(s_top));
    RTF_ASSERT(!r_appliance::read_flag(s_top + PATH_SLASH + "no_such_dir"));
}

void test_r_appliance::test_read_flag_true()
{
    // The file provisioning writes (spec §1.3): only "appliance" matters.
    _write(R"({"appliance": true, "claim_secret": "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "label": "pilot-001", "provisioned_at": "2026-11-16T00:00:00Z", "allow_exec": false})");
    RTF_ASSERT(r_appliance::read_flag(s_top));

    _write(R"({"appliance": true})");
    RTF_ASSERT(r_appliance::read_flag(s_top));
}

void test_r_appliance::test_read_flag_false_or_bad()
{
    const vector<string> off = {
        R"({"appliance": false, "claim_secret": "X"})",
        R"({"claim_secret": "X"})",
        R"({"appliance": "true"})",
        R"({"appliance": 1})",
        R"([true])",
        R"(true)",
        R"({"appliance": tru)",
        "",
    };
    for(const auto& contents : off)
    {
        _write(contents);
        RTF_ASSERT(!r_appliance::read_flag(s_top));
    }
}

void test_r_appliance::test_read_flag_never_logs_secret()
{
    const string secret = "SECRETSECRETSECRETSECRET42";
    mutex lock;
    vector<string> logged;
    r_logger::set_log_callback([&](r_logger::LOG_LEVEL, const string& msg) {
        lock_guard<mutex> g(lock);
        logged.push_back(msg);
    });

    // Good, wrong-typed and truncated files: none may quote the secret.
    _write(R"({"appliance": true, "claim_secret": ")" + secret + R"("})");
    RTF_ASSERT(r_appliance::read_flag(s_top));
    _write(R"({"appliance": "yes", "claim_secret": ")" + secret + R"("})");
    RTF_ASSERT(!r_appliance::read_flag(s_top));
    _write(R"({"claim_secret": ")" + secret + R"(", "appliance": tr)");
    RTF_ASSERT(!r_appliance::read_flag(s_top));

    r_logger::clear_log_callback();
    lock_guard<mutex> g(lock);
    RTF_ASSERT(!logged.empty());   // the bad files were reported...
    for(const auto& m : logged)
        RTF_ASSERT(m.find(secret) == string::npos);   // ...without the secret
}

void test_r_appliance::test_auth_status_fields()
{
    auto j = r_appliance::auth_status(true, true, false);
    RTF_ASSERT(j.is_object());
    RTF_ASSERT(j.size() == 3);
    RTF_ASSERT(j["password_set"].is_boolean() && j["password_set"].get<bool>());
    RTF_ASSERT(j["appliance"].is_boolean() && j["appliance"].get<bool>());
    RTF_ASSERT(j["local"].is_boolean() && !j["local"].get<bool>());

    j = r_appliance::auth_status(false, false, true);
    RTF_ASSERT(!j["password_set"].get<bool>());
    RTF_ASSERT(!j["appliance"].get<bool>());
    RTF_ASSERT(j["local"].get<bool>());
}

void test_r_appliance::test_set_password_bootstrap()
{
    // No password yet: from loopback only, appliance or not.
    for(bool appliance : {false, true})
        for(auto a : {auth::none, auth::current_password, auth::session_token, auth::local_token})
            RTF_ASSERT(r_appliance::check_set_password(appliance, false, false, a) == verdict::loopback_only);

    // A normal install needs no credentials on loopback (unchanged).
    for(auto a : {auth::none, auth::current_password, auth::session_token, auth::local_token})
        RTF_ASSERT(r_appliance::check_set_password(false, false, true, a) == verdict::allow);

    // An appliance needs the local token too: only its agent sets the first one.
    RTF_ASSERT(r_appliance::check_set_password(true, false, true, auth::local_token) == verdict::allow);
    for(auto a : {auth::none, auth::current_password, auth::session_token})
        RTF_ASSERT(r_appliance::check_set_password(true, false, true, a) == verdict::cloud_managed);
}

void test_r_appliance::test_set_password_normal_install()
{
    // Unchanged: a token or the current password, from anywhere.
    for(bool loopback : {false, true})
    {
        RTF_ASSERT(r_appliance::check_set_password(false, true, loopback, auth::none) == verdict::unauthorized);
        RTF_ASSERT(r_appliance::check_set_password(false, true, loopback, auth::current_password) == verdict::allow);
        RTF_ASSERT(r_appliance::check_set_password(false, true, loopback, auth::session_token) == verdict::allow);
        RTF_ASSERT(r_appliance::check_set_password(false, true, loopback, auth::local_token) == verdict::allow);
    }
}

void test_r_appliance::test_set_password_appliance()
{
    // Only the local token changes a password that is set; the rest get a 403.
    for(bool loopback : {false, true})
    {
        RTF_ASSERT(r_appliance::check_set_password(true, true, loopback, auth::local_token) == verdict::allow);
        RTF_ASSERT(r_appliance::check_set_password(true, true, loopback, auth::current_password) == verdict::cloud_managed);
        RTF_ASSERT(r_appliance::check_set_password(true, true, loopback, auth::session_token) == verdict::cloud_managed);
        RTF_ASSERT(r_appliance::check_set_password(true, true, loopback, auth::none) == verdict::cloud_managed);
    }
}

void test_r_appliance::test_login_password_appliance()
{
    const string stored = "K7QM-X2PR-9DTF";
    // As shown, without the hyphens, in lower case, with spaces, or all of these.
    for(const string typed : {"K7QM-X2PR-9DTF", "K7QMX2PR9DTF", "k7qm-x2pr-9dtf", "K7QM X2PR 9DTF", " k7qm x2pr-9dtf\n", "\tK7QM--X2PR 9dtf"})
        RTF_ASSERT(r_appliance::login_password_matches(true, typed, stored));
    // A wrong one is still refused: another character, one short or one over, or nothing.
    for(const string typed : {"K7QM-X2PR-9DTG", "K7QM-X2PR-9DT", "K7QM-X2PR-9DTFF", "", "---", "    "})
        RTF_ASSERT(!r_appliance::login_password_matches(true, typed, stored));
    // The older 20-character form works the same way.
    RTF_ASSERT(r_appliance::login_password_matches(true, "abcd efgh jkmn pqrs tuvw", "ABCD-EFGH-JKMN-PQRS-TUVW"));
    // A stored password that normalizes to nothing matches nothing, not even itself.
    RTF_ASSERT(!r_appliance::login_password_matches(true, "", ""));
    RTF_ASSERT(!r_appliance::login_password_matches(true, "- -", "- -"));

    RTF_ASSERT(r_appliance::normalize_password(" ab-c\td\r\nE-9 ") == "ABCDE9");
    // Only ASCII letters change case; anything else is kept as it is.
    RTF_ASSERT(r_appliance::normalize_password("\xc3\xa9_x.") == "\xc3\xa9_X.");
}

void test_r_appliance::test_login_password_normal_install()
{
    // A person's own password on any other install is compared exactly.
    RTF_ASSERT(r_appliance::login_password_matches(false, "My-Pass word", "My-Pass word"));
    for(const string typed : {"my-pass word", "MY-PASS WORD", "MyPassword", "My-Password", "My Pass word"})
        RTF_ASSERT(!r_appliance::login_password_matches(false, typed, "My-Pass word"));
}
