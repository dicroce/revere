
#include "r_http/r_http_exception.h"

using namespace r_http;
using namespace r_utils;
using namespace std;

r_http_io_exception::r_http_io_exception() :
    r_exception()
{
}

r_http_io_exception::r_http_io_exception( const char* msg, ... ) :
    r_exception()
{
    va_list args;
    va_start( args, msg );
    set_msg( r_string_utils::format( msg, args ) );
    va_end( args );
}

r_http_exception_generic::r_http_exception_generic() :
    r_exception()
{
}

r_http_exception_generic::r_http_exception_generic( const char* msg, ... ) :
    r_exception()
{
    va_list args;
    va_start( args, msg );
    set_msg( r_string_utils::format( msg, args ) );
    va_end( args );
}

r_http_exception_generic::r_http_exception_generic( const string& msg ) :
    r_exception( msg )
{
}

r_http_exception::r_http_exception( int statusCode ) :
    r_http_exception_generic(),
    _statusCode( statusCode )
{
}

r_http_exception::r_http_exception( int statusCode, const char* msg, ... ) :
    r_http_exception_generic(),
    _statusCode( statusCode )
{
    va_list args;
    va_start( args, msg );
    set_msg( r_string_utils::format( msg, args ) );
    va_end( args );
}

r_http_exception::r_http_exception( int statusCode, const string& msg ) :
    r_http_exception_generic( msg ),
    _statusCode( statusCode )
{
}

r_http_400_exception::r_http_400_exception() :
    r_http_exception( 400 )
{
}

r_http_400_exception::r_http_400_exception( const char* msg, ... ) :
    r_http_exception( 400 )
{
    va_list args;
    va_start( args, msg );
    set_msg( r_string_utils::format( msg, args ) );
    va_end( args );
}

r_http_400_exception::r_http_400_exception( const string& msg ) :
    r_http_exception( 400, msg )
{
}

r_http_401_exception::r_http_401_exception() :
    r_http_exception( 401 )
{
}

r_http_401_exception::r_http_401_exception( const char* msg, ... ) :
    r_http_exception( 401 )
{
    va_list args;
    va_start( args, msg );
    set_msg( r_string_utils::format( msg, args ) );
    va_end( args );
}

r_http_401_exception::r_http_401_exception( const string& msg ) :
    r_http_exception( 401, msg )
{
}

r_http_403_exception::r_http_403_exception() :
    r_http_exception( 403 )
{
}

r_http_403_exception::r_http_403_exception( const char* msg, ... ) :
    r_http_exception( 403 )
{
    va_list args;
    va_start( args, msg );
    set_msg( r_string_utils::format( msg, args ) );
    va_end( args );
}

r_http_403_exception::r_http_403_exception( const string& msg ) :
    r_http_exception( 403, msg )
{
}

r_http_404_exception::r_http_404_exception() :
    r_http_exception( 404 )
{
}

r_http_404_exception::r_http_404_exception( const char* msg, ... ) :
    r_http_exception( 404 )
{
    va_list args;
    va_start( args, msg );
    set_msg( r_string_utils::format( msg, args ) );
    va_end( args );
}

r_http_404_exception::r_http_404_exception( const string& msg ) :
    r_http_exception( 404, msg )
{
}

r_http_415_exception::r_http_415_exception() :
    r_http_exception( 415 )
{
}

r_http_415_exception::r_http_415_exception( const char* msg, ... ) :
    r_http_exception( 415 )
{
    va_list args;
    va_start( args, msg );
    set_msg( r_string_utils::format( msg, args ) );
    va_end( args );
}

r_http_415_exception::r_http_415_exception( const string& msg ) :
    r_http_exception( 415, msg )
{
}

r_http_453_exception::r_http_453_exception() :
    r_http_exception( 453 )
{
}

r_http_453_exception::r_http_453_exception( const char* msg, ... ) :
    r_http_exception( 453 )
{
    va_list args;
    va_start( args, msg );
    set_msg( r_string_utils::format( msg, args ) );
    va_end( args );
}

r_http_453_exception::r_http_453_exception( const string& msg ) :
    r_http_exception( 453, msg )
{
}

r_http_500_exception::r_http_500_exception() :
    r_http_exception( 500 )
{
}

r_http_500_exception::r_http_500_exception( const char* msg, ... ) :
    r_http_exception( 500 )
{
    va_list args;
    va_start( args, msg );
    set_msg( r_string_utils::format( msg, args ) );
    va_end( args );
}

r_http_500_exception::r_http_500_exception( const string& msg ) :
    r_http_exception( 500, msg )
{
}

r_http_501_exception::r_http_501_exception() :
    r_http_exception( 501 )
{
}

r_http_501_exception::r_http_501_exception( const char* msg, ... ) :
    r_http_exception( 501 )
{
    va_list args;
    va_start( args, msg );
    set_msg( r_string_utils::format( msg, args ) );
    va_end( args );
}

r_http_501_exception::r_http_501_exception( const string& msg ) :
    r_http_exception( 501, msg )
{
}

void r_http::throw_r_http_exception( int statusCode, const char* msg, ... )
{
    va_list args;
    va_start( args, msg );
    const string message = r_string_utils::format( msg, args );
    va_end( args );

    r_http::throw_r_http_exception( statusCode, message );
}

void r_http::throw_r_http_exception( int statusCode, const string& msg )
{
    switch( statusCode )
    {
        case 400:
        {
            r_http_400_exception e( msg );
            throw e;
        }
        case 401:
        {
            r_http_401_exception e( msg );
            throw e;
        }
        case 403:
        {
            r_http_403_exception e( msg );
            throw e;
        }
        case 404:
        {
            r_http_404_exception e( msg );
            throw e;
        }
        case 415:
        {
            r_http_415_exception e( msg );
            throw e;
        }
        case 453:
        {
            r_http_453_exception e( msg );
            throw e;
        }
        case 500:
        {
            r_http_500_exception e( msg );
            throw e;
        }
        case 501:
        {
            r_http_501_exception e( msg );
            throw e;
        }
        default:
        {
            r_http_exception e( statusCode, msg );
            throw e;
        }
    }
}

