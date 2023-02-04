
#include "r_http/r_range_request_generator.h"

using namespace r_utils;
using namespace r_http;
using namespace std;

r_range_request_generator::r_range_request_generator(const string& host, int port, const string& uri, size_t totalSize, size_t requestSize, size_t pos) :
    _host(host),
    _port(port),
    _uri(uri),
    _totalSize(totalSize),
    _requestSize(requestSize),
    _pos(pos)
{
}

r_range_request_generator::~r_range_request_generator() noexcept
{
}

bool r_range_request_generator::valid() const
{
    return _pos < _totalSize;
}

r_client_request r_range_request_generator::get() const
{
    r_client_request req(_host, _port);
    req.set_method(METHOD_GET);
    req.set_uri(_uri);

    size_t be = _pos + (_requestSize-1);
    if(be >= _totalSize)
        be = (_totalSize-1);

    req.add_header("Range", r_string_utils::format("bytes=%u-%u",_pos,be));

    return req;
}

void r_range_request_generator::next()
{
    size_t be = _pos + (_requestSize-1);
    if(be >= _totalSize)
        be = (_totalSize-1);

    _pos = be + 1;
}
