
#include "layouts.h"
#include "r_utils/r_exception.h"

using namespace vision;
using namespace std;

string vision::layout_to_s(layout l)
{
    if(l == LAYOUT_ONE_BY_ONE)
        return "onebyone";
    else if(l == LAYOUT_TWO_BY_TWO)
        return "twobytwo";
    else if(l == LAYOUT_FOUR_BY_FOUR)
        return "fourbyfour";
    R_THROW(("Unknown layout!"));
}

layout vision::s_to_layout(const string& s)
{
    if(s == "onebyone")
        return LAYOUT_ONE_BY_ONE;
    else if(s == "twobytwo")
        return LAYOUT_TWO_BY_TWO;
    else if(s == "fourbyfour")
        return LAYOUT_FOUR_BY_FOUR;
    R_THROW(("Unknown layout string!"));
}
