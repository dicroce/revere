
#ifndef __vision_configure_state_h
#define __vision_configure_state_h

#include <string>
#include <map>
#include <vector>
#include "r_utils/r_nullable.h"
#include "layouts.h"
#include "stream_info.h"

namespace vision
{

struct configure_state
{
    r_utils::r_nullable<std::string> revere_ipv4 {"127.0.0.1"};
    layout current_layout {LAYOUT_ONE_BY_ONE};
    std::map<std::string, stream_info> stream_map;

    mutable bool _need_save {false};

    void load();
    bool need_save();
    void save();

    void set_revere_ipv4(const std::string& ip);
    r_utils::r_nullable<std::string> get_revere_ipv4() const;

    void set_current_layout(layout l);
    layout get_current_layout() const;

    std::string make_name(int window, layout l, int index) const;
    void set_stream_info(const stream_info& si);
    void unset_stream_info(const std::string& name);
    r_utils::r_nullable<stream_info> get_stream_info(const std::string& name) const;
    std::vector<stream_info> collect_stream_info(int window, layout l) const;

};

}

#endif
