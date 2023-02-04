
#ifndef r_utils_r_socket_address_h_
#define r_utils_r_socket_address_h_

#include <string>
#include "r_utils/r_macro.h"
#ifdef IS_WINDOWS
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <BaseTsd.h>
#endif

#ifdef IS_LINUX
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#endif

namespace r_utils
{

/// String constant that can be used with XSocketAddress to represent the IPv4 INADDR_ANY enumeration.
const std::string ip4_addr_any("INADDR_ANY");

/// String constant that can be used with XSocketAddress to represent the IPv6 IN6ADDR_ANY enumeration.
const std::string ip6_addr_any("IN6ADDR_ANY");

/// This class provides a common way to deal with IPv4 and IPv6 addresses.
class r_socket_address final
{
public:
    R_API r_socket_address(int port, const std::string& address = ip4_addr_any);

    R_API r_socket_address(const struct sockaddr* addr, const socklen_t len);

    R_API r_socket_address(r_socket_address&& obj) noexcept;

    R_API r_socket_address(const r_socket_address& obj);

    R_API ~r_socket_address() noexcept;

    R_API r_socket_address& operator = (r_socket_address&& obj) noexcept;

    R_API r_socket_address& operator = (const r_socket_address& obj);

    R_API int port() const { return _port; }
    R_API void set_port_num(int port); // Can't be SetPort b/c of stupid Window macro of same name

    R_API const std::string& address() const { return _addr; }

    R_API void set_address(const std::string& addr, int port = -1);

    R_API unsigned int address_family() const { return _sockaddr.ss_family; }

    R_API bool operator==(const r_socket_address&) const;
    R_API bool operator==(const struct sockaddr*) const;
    R_API bool operator==(const struct sockaddr_storage&) const;
    R_API bool operator!=(const r_socket_address&) const;
    R_API bool operator!=(const struct sockaddr*) const;
    R_API bool operator!=(const struct sockaddr_storage&) const;

    R_API struct sockaddr* get_sock_addr() const { return (struct sockaddr*)&_sockaddr; }

    R_API socklen_t sock_addr_size() const { return sock_addr_size(_sockaddr.ss_family); }

    R_API bool is_ipv4() const;
    R_API bool is_ipv6() const;
    R_API bool is_multicast() const;

    R_API bool is_ipv4_mapped_to_ipv6(std::string* unmapped=0) const;

    R_API bool is_wildcard_address() const;

    R_API static unsigned int get_address_family(const std::string& address, struct sockaddr* saddr=0);

    R_API static socklen_t sock_addr_size(unsigned int addrFamily);

    R_API static std::string address_to_string(const struct sockaddr* addr, const socklen_t len);

    R_API static void string_to_address(const std::string& saddr,
                                  struct sockaddr* addr,
                                  const socklen_t len);

    R_API static std::string isolate_address(const std::string& addr);

    R_API static bool is_hostname(const std::string& addr);

    R_API static bool is_ipv4(const std::string& addr);
    R_API static bool is_ipv4(const struct sockaddr* addr, const socklen_t len);
    R_API static bool is_ipv6(const std::string& addr);
    R_API static bool is_ipv6(const struct sockaddr* addr, const socklen_t len);
    R_API static bool is_multicast(const std::string& addr);
    R_API static bool is_multicast(const struct sockaddr* addr, const socklen_t len);
    R_API static bool is_wildcard_address(const std::string& addr);
    R_API static bool is_wildcard_address(const struct sockaddr* addr, const socklen_t len);

    R_API static bool is_ipv4_mapped_to_ipv6(const std::string& addr, std::string* unmapped=0);

    R_API static bool is_ipv4_mapped_to_ipv6(const struct sockaddr* addr,
                                       const socklen_t len,
                                       std::string* unmapped=0);

private:
    int _port;
    std::string _addr;
    struct sockaddr_storage _sockaddr;
};

}

#endif
