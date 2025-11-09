
#ifndef __revere_configure_state_h
#define __revere_configure_state_h

#include <string>
#include <map>
#include <mutex>

namespace revere
{

struct configure_state
{
    std::map<std::string, std::string> config_map;
    mutable bool _need_save {false};
    mutable std::mutex _mutex;

    void load();
    bool need_save();
    void save();

    void set_value(const std::string& key, const std::string& value);
    std::string get_value(const std::string& key, const std::string& default_value = "") const;
    bool get_bool(const std::string& key, bool default_value = false) const;
    void set_bool(const std::string& key, bool value);
};

}

#endif
