#ifndef r_http_methods_h
#define r_http_methods_h

#include "r_utils/r_macro.h"
#include <string>

namespace r_http
{

enum webbie_method
{
    METHOD_GET = 1,
    METHOD_POST = 2,
    METHOD_PUT = 3,
    METHOD_DELETE = 4,
    METHOD_PATCH = 5,
    METHOD_HEAD = 6
};

R_API std::string method_text( int method );

R_API int method_type( const std::string& methodText );

}

#endif