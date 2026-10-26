#include "test_r_ws_auth.h"
#include "r_vss/r_ws_auth.h"
#include "r_utils/r_string_utils.h"
#include <set>
#include <string>

using namespace r_vss;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

REGISTER_TEST_FIXTURE(test_r_ws_auth);

static r_nullable<string> _s(const string& v)
{
    r_nullable<string> n;
    n.set_value(v);
    return n;
}

static const r_nullable<string> NONE;

static bool _has(const string& hay, const string& needle)
{
    return hay.find(needle) != string::npos;
}

static bool _starts(const string& s, const string& prefix)
{
    return s.compare(0, prefix.size(), prefix) == 0;
}

void test_r_ws_auth::test_generate_token()
{
    set<string> seen;
    for(int i = 0; i < 200; ++i)
    {
        auto t = r_ws_auth::generate_token();
        RTF_ASSERT(t.size() == 64);   // 256 bits as hex
        for(char c : t)
            RTF_ASSERT((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
        RTF_ASSERT(seen.insert(t).second);
    }
}

void test_r_ws_auth::test_sessions()
{
    r_ws_auth auth;
    auto t0 = r_ws_auth::clock::now();

    auto a = auth.issue_session(t0);
    auto b = auth.issue_session(t0);
    RTF_ASSERT(a != b);
    RTF_ASSERT(auth.session_count() == 2);

    RTF_ASSERT(auth.valid(a, t0));
    RTF_ASSERT(auth.valid(a, t0 + r_ws_auth::SESSION_LIFETIME - seconds(1)));
    RTF_ASSERT(!auth.valid("nope", t0));
    RTF_ASSERT(!auth.valid("", t0));

    // Expiry drops it for good.
    RTF_ASSERT(!auth.valid(a, t0 + r_ws_auth::SESSION_LIFETIME + seconds(1)));
    RTF_ASSERT(!auth.valid(a, t0));
    RTF_ASSERT(auth.session_count() == 1);

    auth.revoke(b);
    RTF_ASSERT(!auth.valid(b, t0));
    RTF_ASSERT(auth.session_count() == 0);

    auto c = auth.issue_session(t0);
    auto d = auth.issue_session(t0);
    auth.revoke_all_sessions();
    RTF_ASSERT(!auth.valid(c, t0));
    RTF_ASSERT(!auth.valid(d, t0));
}

void test_r_ws_auth::test_local_token()
{
    r_ws_auth auth;
    auto t0 = r_ws_auth::clock::now();

    // No local token yet: an empty string must not match it.
    RTF_ASSERT(!auth.valid("", t0));

    auto local = r_ws_auth::generate_token();
    auth.set_local_token(local);
    RTF_ASSERT(auth.local_token() == local);
    RTF_ASSERT(auth.valid(local, t0));
    // Never expires, and survives a password change.
    RTF_ASSERT(auth.valid(local, t0 + hours(24 * 365)));
    auth.revoke_all_sessions();
    RTF_ASSERT(auth.valid(local, t0));
    auth.revoke(local);
    RTF_ASSERT(auth.valid(local, t0));

    // A near miss is not accepted.
    auto wrong = local;
    wrong.back() = (wrong.back() == 'a') ? 'b' : 'a';
    RTF_ASSERT(!auth.valid(wrong, t0));
    RTF_ASSERT(!auth.valid(local.substr(1), t0));
}

void test_r_ws_auth::test_throttle_per_address()
{
    r_ws_auth auth;
    auto t0 = r_ws_auth::clock::now();
    const string bad = "192.168.1.66", good = "192.168.1.20";

    for(int i = 0; i < r_ws_auth::MAX_FAILED_LOGINS - 1; ++i)
    {
        auth.record_failure(bad, t0);
        RTF_ASSERT(!auth.locked_out(bad, t0));
    }
    auth.record_failure(bad, t0);
    RTF_ASSERT(auth.locked_out(bad, t0));
    RTF_ASSERT(auth.locked_out(bad, t0 + r_ws_auth::LOCKOUT - seconds(1)));

    // Another address is unaffected.
    RTF_ASSERT(!auth.locked_out(good, t0));

    // The lockout ends, and the count starts over.
    auto t1 = t0 + r_ws_auth::LOCKOUT + seconds(1);
    RTF_ASSERT(!auth.locked_out(bad, t1));
    auth.record_failure(bad, t1);
    RTF_ASSERT(!auth.locked_out(bad, t1));

    // Success clears the count.
    for(int i = 0; i < r_ws_auth::MAX_FAILED_LOGINS - 1; ++i)
        auth.record_failure(good, t0);
    auth.record_success(good);
    auth.record_failure(good, t0);
    RTF_ASSERT(!auth.locked_out(good, t0));
}

void test_r_ws_auth::test_throttle_failures_expire()
{
    r_ws_auth auth;
    auto t0 = r_ws_auth::clock::now();
    const string a = "10.0.0.5";

    // Occasional typos spread over hours never add up to a lockout.
    for(int i = 0; i < r_ws_auth::MAX_FAILED_LOGINS * 2; ++i)
    {
        auto t = t0 + minutes(16) * i;
        auth.record_failure(a, t);
        RTF_ASSERT(!auth.locked_out(a, t));
    }
}

void test_r_ws_auth::test_throttle_table_bounded()
{
    r_ws_auth auth;
    auto t0 = r_ws_auth::clock::now();

    // A sweep of addresses long ago...
    for(int i = 0; i < 3000; ++i)
        auth.record_failure("10.1." + to_string(i / 250) + "." + to_string(i % 250), t0);

    // ...is forgotten later, but a current lockout is not.
    auto t1 = t0 + hours(1);
    const string attacker = "10.9.9.9";
    for(int i = 0; i < r_ws_auth::MAX_FAILED_LOGINS; ++i)
        auth.record_failure(attacker, t1);
    for(int i = 0; i < 2000; ++i)
        auth.record_failure("10.2." + to_string(i / 250) + "." + to_string(i % 250), t1 + seconds(1));
    RTF_ASSERT(auth.locked_out(attacker, t1 + seconds(2)));
}

void test_r_ws_auth::test_extract_token()
{
    // Header, case-insensitive scheme, whitespace tolerated.
    RTF_ASSERT(r_ws_auth::extract_token(_s("Bearer abc"), NONE, false).value() == "abc");
    RTF_ASSERT(r_ws_auth::extract_token(_s("bearer abc"), NONE, false).value() == "abc");
    RTF_ASSERT(r_ws_auth::extract_token(_s("  Bearer   abc  "), NONE, false).value() == "abc");

    // Malformed header: null, and the cookie doesn't stand in for it.
    RTF_ASSERT(r_ws_auth::extract_token(_s("Basic abc"), _s("revere_session=xyz"), true).is_null());
    RTF_ASSERT(r_ws_auth::extract_token(_s("Bearer "), _s("revere_session=xyz"), true).is_null());

    // Header wins over cookie.
    RTF_ASSERT(r_ws_auth::extract_token(_s("Bearer abc"), _s("revere_session=xyz"), true).value() == "abc");

    // Cookie only when allowed.
    RTF_ASSERT(r_ws_auth::extract_token(NONE, _s("revere_session=xyz"), true).value() == "xyz");
    RTF_ASSERT(r_ws_auth::extract_token(NONE, _s("revere_session=xyz"), false).is_null());

    // Among other cookies, quoted, and not confused by similar names.
    RTF_ASSERT(r_ws_auth::extract_token(NONE, _s("a=1; revere_session=xyz; b=2"), true).value() == "xyz");
    RTF_ASSERT(r_ws_auth::extract_token(NONE, _s("revere_session=\"xyz\""), true).value() == "xyz");
    RTF_ASSERT(r_ws_auth::extract_token(NONE, _s("not_revere_session=xyz; x_revere_session=q"), true).is_null());
    RTF_ASSERT(r_ws_auth::extract_token(NONE, _s("revere_session="), true).is_null());
    RTF_ASSERT(r_ws_auth::extract_token(NONE, NONE, true).is_null());
}

void test_r_ws_auth::test_cookies()
{
    auto c = r_ws_auth::session_cookie("tok", false);
    RTF_ASSERT(_starts(c, "revere_session=tok;"));
    RTF_ASSERT(_has(c, "HttpOnly"));
    RTF_ASSERT(_has(c, "SameSite=Strict"));
    RTF_ASSERT(_has(c, "Path=/"));
    RTF_ASSERT(_has(c, "Max-Age=86400"));
    RTF_ASSERT(!_has(c, "Secure"));
    RTF_ASSERT(_has(r_ws_auth::session_cookie("tok", true), "; Secure"));

    auto clr = r_ws_auth::clear_session_cookie(true);
    RTF_ASSERT(_starts(clr, "revere_session=;"));
    RTF_ASSERT(_has(clr, "Max-Age=0"));
    RTF_ASSERT(_has(clr, "; Secure"));

    // The cookie a browser sends back parses to the token.
    RTF_ASSERT(r_ws_auth::extract_token(NONE, _s("revere_session=tok"), true).value() == "tok");

    RTF_ASSERT(r_ws_auth::constant_time_equal("abc", "abc"));
    RTF_ASSERT(!r_ws_auth::constant_time_equal("abc", "abd"));
    RTF_ASSERT(!r_ws_auth::constant_time_equal("abc", "ab"));
}
