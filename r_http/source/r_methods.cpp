#include "r_http/r_methods.h"
#include "r_http/r_http_exception.h"

using namespace r_utils;
using namespace std;

string r_http::method_text( int method )
{
    if( method == METHOD_GET )
        return "GET";
    else if( method == METHOD_POST )
        return "POST";
    else if( method == METHOD_PUT )
        return "PUT";
    else if( method == METHOD_DELETE )
        return "DELETE";
    else if( method == METHOD_PATCH )
        return "PATCH";
    else if( method == METHOD_HEAD )
        return "HEAD";

    R_STHROW( r_http_exception_generic, ("Unsupported method.") );
}

int r_http::method_type( const string& methodText )
{
    string lowerMethod = r_string_utils::to_lower(methodText);

    if( lowerMethod == "get" )
        return METHOD_GET;
    else if( lowerMethod == "post" )
        return METHOD_POST;
    else if( lowerMethod == "put" )
        return METHOD_PUT;
    else if( lowerMethod == "delete" )
        return METHOD_DELETE;
    else if( lowerMethod == "patch" )
        return METHOD_PATCH;
    else if( lowerMethod == "head" )
        return METHOD_HEAD;

    R_STHROW( r_http_exception_generic, ("Unsupported method.") );
}
