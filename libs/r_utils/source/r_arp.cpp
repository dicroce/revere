// The OS's neighbour (ARP) table: which hardware (MAC) address a local IPv4
// host has. See r_socket.h.

#include "r_utils/r_socket.h"
#include "r_utils/r_string_utils.h"
#include <cctype>
#include <cstdio>
#include <sstream>
#include <vector>

#ifdef IS_LINUX
#include <fstream>
#include <iterator>
#endif

#ifdef IS_MACOS
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>
#endif

#ifdef IS_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#endif

using namespace r_utils;
using namespace std;

r_nullable<string> r_utils::r_networking::r_normalize_mac(const string& text)
{
    r_nullable<string> none;
    string t = r_string_utils::strip(text);
    vector<string> parts;
    string cur;
    for(char c : t)
    {
        if(c == ':' || c == '-')
        {
            parts.push_back(cur);
            cur.clear();
        }
        else
            cur += c;
    }
    parts.push_back(cur);
    if(parts.size() != 6)
        return none;

    string out;
    bool all_zero = true, all_ff = true;
    for(size_t i = 0; i < parts.size(); ++i)
    {
        auto& p = parts[i];
        if(p.empty() || p.size() > 2)
            return none;
        for(char c : p)
            if(!isxdigit((unsigned char)c))
                return none;
        string byte = p.size() == 1 ? "0" + p : p;
        for(auto& c : byte)
            c = (char)tolower((unsigned char)c);
        if(byte != "00") all_zero = false;
        if(byte != "ff") all_ff = false;
        if(i) out += ":";
        out += byte;
    }
    if(all_zero || all_ff)
        return none;
    r_nullable<string> result;
    result.set_value(out);
    return result;
}

r_nullable<string> r_utils::r_networking::r_parse_proc_net_arp(const string& text, const string& ipv4)
{
    // IP address       HW type     Flags       HW address            Mask     Device
    // 192.168.1.20     0x1         0x2         00:1b:2c:3d:4e:5f     *        eth0
    // Flags 0x2 (ATF_COM) is a complete entry; 0x0 is incomplete.
    istringstream lines(text);
    string line;
    bool header = true;
    while(getline(lines, line))
    {
        if(header) { header = false; continue; }
        istringstream fields(line);
        string ip, hw_type, flags, hw_addr;
        if(!(fields >> ip >> hw_type >> flags >> hw_addr))
            continue;
        if(ip != ipv4)
            continue;
        unsigned long f = 0;
        try { f = stoul(flags, nullptr, 16); } catch(...) { continue; }
        if((f & 0x2) == 0)
            continue;
        return r_normalize_mac(hw_addr);
    }
    return r_nullable<string>();
}

r_nullable<string> r_utils::r_networking::r_arp_lookup(const string& ipv4)
{
    r_nullable<string> none;
    try
    {
#if defined(IS_LINUX)
        ifstream f("/proc/net/arp");
        if(!f)
            return none;
        string text((istreambuf_iterator<char>(f)), istreambuf_iterator<char>());
        return r_parse_proc_net_arp(text, ipv4);
#elif defined(IS_MACOS)
        struct in_addr want;
        if(inet_pton(AF_INET, ipv4.c_str(), &want) != 1)
            return none;
        int mib[6] = {CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_FLAGS, RTF_LLINFO};
        size_t needed = 0;
        if(sysctl(mib, 6, nullptr, &needed, nullptr, 0) < 0 || needed == 0)
            return none;
        vector<char> buf(needed);
        if(sysctl(mib, 6, buf.data(), &needed, nullptr, 0) < 0)
            return none;
        for(char* next = buf.data(); next < buf.data() + needed;)
        {
            auto rtm = reinterpret_cast<struct rt_msghdr*>(next);
            if(rtm->rtm_msglen == 0)
                break;
            auto sin = reinterpret_cast<struct sockaddr_inarp*>(rtm + 1);
            auto sdl = reinterpret_cast<struct sockaddr_dl*>(reinterpret_cast<char*>(sin) + ((sin->sin_len > 0) ? ((sin->sin_len + sizeof(uint32_t) - 1) & ~(sizeof(uint32_t) - 1)) : sizeof(uint32_t)));
            if(sin->sin_addr.s_addr == want.s_addr && sdl->sdl_family == AF_LINK && sdl->sdl_alen == 6)
            {
                auto mac = reinterpret_cast<const unsigned char*>(LLADDR(sdl));
                char text[18];
                snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                return r_normalize_mac(text);
            }
            next += rtm->rtm_msglen;
        }
        return none;
#elif defined(IS_WINDOWS)
        IN_ADDR want;
        if(InetPtonA(AF_INET, ipv4.c_str(), &want) != 1)
            return none;
        PMIB_IPNET_TABLE2 table = nullptr;
        if(GetIpNetTable2(AF_INET, &table) != NO_ERROR || table == nullptr)
            return none;
        r_nullable<string> found;
        for(ULONG i = 0; i < table->NumEntries; ++i)
        {
            const MIB_IPNET_ROW2& row = table->Table[i];
            if(row.Address.si_family != AF_INET || row.Address.Ipv4.sin_addr.S_un.S_addr != want.S_un.S_addr)
                continue;
            if(row.PhysicalAddressLength != 6)
                continue;
            if(row.State == NlnsUnreachable || row.State == NlnsIncomplete)
                continue;
            char text[18];
            snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x",
                     row.PhysicalAddress[0], row.PhysicalAddress[1], row.PhysicalAddress[2],
                     row.PhysicalAddress[3], row.PhysicalAddress[4], row.PhysicalAddress[5]);
            found = r_normalize_mac(text);
            break;
        }
        FreeMibTable(table);
        return found;
#else
        return none;
#endif
    }
    catch(...)
    {
        return none;
    }
}
