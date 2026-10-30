
#ifndef __revere_browser_url_h
#define __revere_browser_url_h

#include <string>

namespace revere
{

// True for an http:// or https:// URL with a host, and no spaces, quotes,
// backslashes or control characters. Only these are handed to the browser:
// plugin status messages carry links, and a plugin's text must not be able to
// open a file, an app's own URL scheme, or anything else the OS can open.
bool is_web_url(const std::string& url);

}

#endif
