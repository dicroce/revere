#include "test_browser_url.h"
#include "browser_url.h"
#include <string>

using namespace revere;
using namespace std;

REGISTER_TEST_FIXTURE(test_browser_url);

void test_browser_url::test_web_urls_allowed()
{
    RTF_ASSERT(is_web_url("https://revere.video"));
    RTF_ASSERT(is_web_url("http://localhost:8088"));
    RTF_ASSERT(is_web_url("HTTPS://example.com/path?a=1&b=2#frag"));
    RTF_ASSERT(is_web_url("http://192.168.1.20:11001/camera/abc"));
    RTF_ASSERT(is_web_url("https://user@example.com/"));
}

void test_browser_url::test_other_schemes_refused()
{
    RTF_ASSERT(!is_web_url("file:///etc/passwd"));
    RTF_ASSERT(!is_web_url("/Applications/Calculator.app"));
    RTF_ASSERT(!is_web_url("javascript:alert(1)"));
    RTF_ASSERT(!is_web_url("smb://server/share"));
    RTF_ASSERT(!is_web_url("x-apple.systempreferences:com.apple.preference"));
    RTF_ASSERT(!is_web_url("ftp://example.com"));
    RTF_ASSERT(!is_web_url("httpx://example.com"));
    RTF_ASSERT(!is_web_url("-a Terminal"));
    RTF_ASSERT(!is_web_url(""));
}

void test_browser_url::test_malformed_refused()
{
    // Characters a shell or ShellExecute would treat specially, and control characters
    RTF_ASSERT(!is_web_url("https://example.com\" ; touch /tmp/x \""));
    RTF_ASSERT(!is_web_url("https://example.com/$(id)'"));
    RTF_ASSERT(!is_web_url("https://example.com/`id`"));
    RTF_ASSERT(!is_web_url("https://example.com/a b"));
    RTF_ASSERT(!is_web_url("https://example.com/\nfile:///x"));
    RTF_ASSERT(!is_web_url("https://exa\\mple.com"));
    // No host
    RTF_ASSERT(!is_web_url("https://"));
    RTF_ASSERT(!is_web_url("http:///path"));
    RTF_ASSERT(!is_web_url("https://user@/path"));
    RTF_ASSERT(!is_web_url("https://:8080/"));
    RTF_ASSERT(!is_web_url("https://example.com/" + string(3000, 'a')));
}
