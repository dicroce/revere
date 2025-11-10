
#ifndef __revere_utils_h
#define __revere_utils_h

#include <string>
#include <vector>
#include <map>

namespace revere
{

std::string top_dir();
std::string sub_dir(const std::string& subdir);
std::string join_path(const std::string& path, const std::string& fileName);

// Open URL in default browser (cross-platform)
bool open_url_in_browser(const std::string& url);

}

#endif
