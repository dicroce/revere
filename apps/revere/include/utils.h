
#ifndef __revere_utils_h
#define __revere_utils_h

#include <string>
#include <vector>
#include <map>

namespace revere
{

// Override the base data directory (db, video, logs, config). Must be called
// before the first top_dir()/sub_dir() use. Empty string = platform default
// (~/Documents/revere/revere etc.). Lets a headless/daemon install point storage
// at an explicit path (e.g. /var/lib/revere) and avoids the home/passwd lookup
// that fails under minimal/dynamic service users.
void set_data_dir(const std::string& dir);
std::string top_dir();
std::string sub_dir(const std::string& subdir);
std::string join_path(const std::string& path, const std::string& fileName);

// Open URL in default browser (cross-platform)
bool open_url_in_browser(const std::string& url);

}

#endif
