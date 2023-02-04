
#include "r_utils/r_socket_address.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_std_utils.h"
#include <cctype>
#include <string.h>

using namespace std;
using namespace r_utils;
using namespace r_utils::r_std_utils;

r_socket_address::r_socket_address(int port, const string& address) :
    _port(port),
    _addr(address)
{
    set_address(address);
}

r_socket_address::r_socket_address(const struct sockaddr* addr, const socklen_t len) :
    _port(0),
    _addr()
{
    memset(&_sockaddr, 0, sizeof(_sockaddr));
    memcpy(&_sockaddr, addr, len);

    if (_sockaddr.ss_family == AF_INET)
        _port = ntohs( ((struct sockaddr_in*)&_sockaddr)->sin_port );
    else if (_sockaddr.ss_family == AF_INET6)
        _port = ntohs( ((struct sockaddr_in6*)&_sockaddr)->sin6_port );

    _addr = r_socket_address::address_to_string(addr, len);
}

r_socket_address::r_socket_address( r_socket_address&& obj ) noexcept :
    _port( std::move( obj._port ) ),
    _addr( std::move( obj._addr ) )
{
    memcpy( &_sockaddr, &obj._sockaddr, sizeof( struct sockaddr_storage ) );
}

r_socket_address::r_socket_address( const r_socket_address& obj ) :
    _port( obj._port ),
    _addr( obj._addr )
{
    memcpy( &_sockaddr, &obj._sockaddr, sizeof( struct sockaddr_storage ) );
}

r_socket_address::~r_socket_address() noexcept
{
}

r_socket_address& r_socket_address::operator = ( r_socket_address&& obj ) noexcept
{
    _port = std::move(obj._port);
    _addr = std::move(obj._addr);
    memcpy( &_sockaddr, &obj._sockaddr, sizeof( struct sockaddr_storage ) );
    return *this;
}

r_socket_address& r_socket_address::operator = ( const r_socket_address& obj )
{
    _port = obj._port;
    _addr = obj._addr;
    memcpy( &_sockaddr, &obj._sockaddr, sizeof( struct sockaddr_storage ) );
    return *this;
}

void r_socket_address::set_port_num(int port)
{
    _port = port;
    if (_sockaddr.ss_family == AF_INET)
        ((struct sockaddr_in*)&_sockaddr)->sin_port = htons( _port );
    else if (_sockaddr.ss_family == AF_INET6)
        ((struct sockaddr_in6*)&_sockaddr)->sin6_port = htons( _port );
}

void r_socket_address::set_address(const string& addr, int port)
{
    if (port >= 0)
        _port = port;

    memset(&_sockaddr, 0, sizeof(_sockaddr));
    if (addr.length() == 0 || addr == ip4_addr_any)
    {
        // IPv4 any address
        struct sockaddr_in* pa = (struct sockaddr_in*)&_sockaddr;
        pa->sin_family = AF_INET;
        pa->sin_port = htons( _port );
        pa->sin_addr.s_addr = htonl(INADDR_ANY);
    }
    else if (addr == ip6_addr_any)
    {
        // IPv6 any address
        struct sockaddr_in6* pa = (struct sockaddr_in6*)&_sockaddr;
        pa->sin6_family = AF_INET6;
        pa->sin6_port = htons( _port );
        pa->sin6_addr = in6addr_any;
    }
    else
    {
        // Determine address type and set sockaddr_storage structure.
        get_address_family(addr, (struct sockaddr*)&_sockaddr);

        if (_sockaddr.ss_family == AF_INET)
        {
            struct sockaddr_in* pa = (struct sockaddr_in*)&_sockaddr;
            pa->sin_port = htons( _port );
        }
        else if (_sockaddr.ss_family == AF_INET6)
        {
            struct sockaddr_in6* pa = (struct sockaddr_in6*)&_sockaddr;
            pa->sin6_port = htons( _port );
        }
        else
            R_THROW(("r_socket_address::set_address: Unknown address family (%d) for address \'%s\'", _sockaddr.ss_family, addr.c_str()));
    }


    _addr = addr;
}

unsigned int r_socket_address::get_address_family(const string& address, struct sockaddr* saddr)
{
    raii_ptr<struct addrinfo> info(
        [](struct addrinfo* p){freeaddrinfo(p);}
    );

    struct addrinfo hint;//, *info = 0;
    memset(&hint, 0, sizeof(hint));
    hint.ai_family = AF_UNSPEC;
    hint.ai_flags = AI_CANONNAME;
    //hint.ai_flags = AI_NUMERICHOST;  // Uncomment this to disable DNS lookup

    auto ret = getaddrinfo(isolate_address(address).c_str(), 0, &hint, &info.raw());

    if(ret != 0)
        R_STHROW(r_internal_exception, ("r_socket_address::get_address_family: Failed to determine address info for \'%s\'. %s", address.c_str(), gai_strerror(ret)));

    auto family = info.get()->ai_family;

    if (saddr)
        memcpy(saddr, info.get()->ai_addr, sock_addr_size(family));

    return family;
}

bool r_socket_address::operator==(const r_socket_address& other) const
{
    return *this == other.get_sock_addr();
}

bool r_socket_address::operator==(const struct sockaddr* other) const
{
    size_t len = 0;
    if (other->sa_family == AF_INET)
        len = sizeof(struct sockaddr_in);
    else if (other->sa_family == AF_INET6)
        len = sizeof(struct sockaddr_in6);
    else
        R_STHROW(r_internal_exception, ("r_socket_address::operator==: Unknown address family (%u)", other->sa_family));

    return memcmp(get_sock_addr(), other, len) == 0;
}

bool r_socket_address::operator==(const struct sockaddr_storage& other) const
{
    return *this == (struct sockaddr*)&other;
}

bool r_socket_address::operator!=(const r_socket_address& other) const
{
    return !(*this == other.get_sock_addr());
}

bool r_socket_address::operator!=(const struct sockaddr* other) const
{
    return !(*this == other);
}

bool r_socket_address::operator!=(const struct sockaddr_storage& other) const
{
    return !(*this ==(struct sockaddr*)&other);
}

socklen_t r_socket_address::sock_addr_size(unsigned int addrFamily)
{
    if (addrFamily == AF_INET)
        return sizeof(struct sockaddr_in);
    else if (addrFamily == AF_INET6)
        return sizeof(struct sockaddr_in6);
    R_STHROW(r_internal_exception, ("r_socket_address::sock_addr_size(): Unknown socket address family (%d)", addrFamily));
}

string r_socket_address::address_to_string(const struct sockaddr* addr, const socklen_t len)
{
    string result;

    char tmp[INET6_ADDRSTRLEN];
    void* pa = 0;
    if (addr->sa_family == AF_INET)
        pa = &(((struct sockaddr_in*)addr)->sin_addr);
    else if (addr->sa_family == AF_INET6)
        pa = &(((struct sockaddr_in6*)addr)->sin6_addr);
    else
        R_STHROW(r_internal_exception, ("r_socket_address::address_to_string(): Unknown address family (%d)", addr->sa_family));
    const char* s = inet_ntop(addr->sa_family, pa, tmp, INET6_ADDRSTRLEN);
    if (!s)
        R_STHROW(r_internal_exception, ("address_to_string() failed.()"));
    result = s;

    return result;
}

void r_socket_address::string_to_address(const string& saddr, struct sockaddr* addr, const socklen_t len)
{
    r_socket_address a(0, saddr);
    if (a.sock_addr_size() > len)
        R_STHROW(r_internal_exception, ("r_socket_address:string_to_address: Address structure not large enough to hold result. len (%d)", len));
    memcpy(addr, a.get_sock_addr(), a.sock_addr_size());
}

string r_socket_address::isolate_address(const string& addr)
{
    // If "[]" is present, return the value between the brackets.
    size_t spos = addr.find_first_of('[');
    if (spos == std::string::npos)
        return addr;
    size_t epos = addr.find_last_of(']');
    if (epos == std::string::npos)
        return addr;
    ++spos;
    return addr.substr(spos, epos-spos);
}

bool r_socket_address::is_ipv4() const
{
    return _sockaddr.ss_family == AF_INET;
}

bool r_socket_address::is_ipv6() const
{
    return _sockaddr.ss_family == AF_INET6;
}

bool r_socket_address::is_multicast() const
{
    if (is_ipv6())
    {
        struct sockaddr_in6* a6 = (struct sockaddr_in6*)&_sockaddr;
        return IN6_IS_ADDR_MULTICAST(&a6->sin6_addr);
    }
    else
    {
        struct sockaddr_in* a4 = (struct sockaddr_in*)&_sockaddr;
        uint8_t firstPart = ntohl(a4->sin_addr.s_addr) >> 24;
        if( firstPart >= 224 && firstPart <= 239 )
            return true;
        return false;
    }
}

bool r_socket_address::is_ipv4_mapped_to_ipv6(string* unmapped) const
{
    if (is_ipv6())
    {
        struct sockaddr_in6* a6 = (struct sockaddr_in6*)&_sockaddr;
        if (IN6_IS_ADDR_V4MAPPED(&a6->sin6_addr))
        {
            if (unmapped)
                *unmapped = r_string_utils::format("%u.%u.%u.%u",
                                             a6->sin6_addr.s6_addr[12],
                                             a6->sin6_addr.s6_addr[13],
                                             a6->sin6_addr.s6_addr[14],
                                             a6->sin6_addr.s6_addr[15]);
            return true;
        }
    }
    return false;
}

bool r_socket_address::is_wildcard_address() const
{
    if (is_ipv6())
    {
        static uint8_t wildcard[16] = {0};
        struct sockaddr_in6* a6 = (struct sockaddr_in6*)&_sockaddr;
        return memcmp(a6->sin6_addr.s6_addr, wildcard, sizeof(wildcard)) == 0;
    }
    else
    {
        static uint8_t wildcard[4] = {0};
        struct sockaddr_in* a4 = (struct sockaddr_in*)&_sockaddr;
        return memcmp(&a4->sin_addr.s_addr, wildcard, sizeof(wildcard)) == 0;
    }
}

bool r_socket_address::is_hostname(const string& addr)
{
    // ABNF grammar for a hostname (RFC 2396, section 3.2.2):
    // hostname      = *( domainlabel "." ) toplabel [ "." ]
    // domainlabel   = alphanum | alphanum *( alphanum | "-" ) alphanum
    // toplabel      = alpha | alpha *( alphanum | "-" ) alphanum
    if(addr.empty())
        return false;

    // check toplabel
    const size_t lastDot = addr.find_last_of('.', addr.size() - 2);
    const size_t topLabelStart = lastDot == string::npos ? 0 : lastDot + 1;
    const size_t topLabelLast = addr.size() - (addr[addr.size() - 1] == '.' ? 2 : 1);

    if(!isalpha(addr[topLabelStart]))
        return false;

    for(size_t i = topLabelStart + 1; i < topLabelLast; ++i)
    {
        if(!isalnum(addr[i]) && addr[i] != '-')
            return false;
    }

    if(!isalnum(addr[topLabelLast]))
        return false;

    //check domainlabel
    for(size_t i = 0; i < topLabelStart; )
    {
        if(!isalnum(addr[i++]))
            return false;

        const size_t nextDot = addr.find('.', i);
        if(nextDot == string::npos)
            return false;

        if(nextDot == i)
        {
            ++i;
            continue;
        }

        for(const size_t lastAlphaNum = nextDot - 1; i < lastAlphaNum; ++i)
        {
            if(!isalnum(addr[i]) && addr[i] != '-')
                return false;
        }

        if(!isalnum(addr[i]))
            return false;

        i += 2;
    }

    return true;
}

// STATIC versions
bool r_socket_address::is_ipv4(const string& addr)
{
    return r_socket_address(0, addr).is_ipv4();
}

bool r_socket_address::is_ipv4(const struct sockaddr* addr, const socklen_t len)
{
    return addr->sa_family == AF_INET;
}

bool r_socket_address::is_ipv6(const string& addr)
{
    return r_socket_address(0, addr).is_ipv6();
}

bool r_socket_address::is_ipv6(const struct sockaddr* addr, const socklen_t len)
{
    return addr->sa_family == AF_INET6;
}

bool r_socket_address::is_multicast(const string& addr)
{
    return r_socket_address(0, addr).is_multicast();
}

bool r_socket_address::is_multicast(const struct sockaddr* addr, const socklen_t len)
{
    return r_socket_address(addr, len).is_multicast();
}

bool r_socket_address::is_ipv4_mapped_to_ipv6(const string& addr, string* unmapped)
{
    return r_socket_address(0, addr).is_ipv4_mapped_to_ipv6(unmapped);
}

bool r_socket_address::is_ipv4_mapped_to_ipv6(const struct sockaddr* addr, const socklen_t len, string* unmapped)
{
    return r_socket_address(addr, len).is_ipv4_mapped_to_ipv6(unmapped);
}

bool r_socket_address::is_wildcard_address(const string& addr)
{
    return r_socket_address(0, addr).is_wildcard_address();
}

bool r_socket_address::is_wildcard_address(const struct sockaddr* addr, const socklen_t len)
{
    return r_socket_address(addr, len).is_wildcard_address();
}
