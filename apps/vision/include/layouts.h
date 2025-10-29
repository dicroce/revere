
#ifndef __vision_layouts_h
#define __vision_layouts_h

#include <string>

namespace vision
{

enum layout
{
    LAYOUT_ONE_BY_ONE,
    LAYOUT_TWO_BY_TWO,
    LAYOUT_FOUR_BY_FOUR
};

std::string layout_to_s(layout l);
layout s_to_layout(const std::string& s);

}

#endif
