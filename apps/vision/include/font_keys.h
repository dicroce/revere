
#ifndef __vision_font_keys_h
#define __vision_font_keys_h

#include "r_utils/r_string_utils.h"
#include <string>

namespace vision
{

inline std::string get_font_key_24() {
#ifdef IS_MACOS
    return r_utils::r_string_utils::float_to_s(18.0f, 2);
#else
    return "24.00";
#endif
}

inline std::string get_font_key_18() {
#ifdef IS_MACOS
    return r_utils::r_string_utils::float_to_s(14.0f, 2);
#else
    return "18.00";
#endif
}

inline std::string get_font_key_16() {
#ifdef IS_MACOS
    return r_utils::r_string_utils::float_to_s(13.0f, 2);
#else
    return "16.00";
#endif
}

inline std::string get_font_key_14() {
#ifdef IS_MACOS
    return r_utils::r_string_utils::float_to_s(12.0f, 2);
#else
    return "14.00";
#endif
}

}

#endif
