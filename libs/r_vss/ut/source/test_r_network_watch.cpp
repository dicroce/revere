#include "test_r_network_watch.h"
#include "r_vss/r_network_watch.h"
#include "r_utils/r_socket.h"
#include <map>
#include <memory>

using namespace std;
using namespace std::chrono;
using namespace r_vss;

REGISTER_TEST_FIXTURE(test_r_network_watch);

namespace
{
const auto T0 = r_network_watch::clock::time_point(hours(100));
r_network_watch::clock::time_point at(int ms) { return T0 + milliseconds(ms); }

// A network the test changes by hand, and how many times it was looked at.
struct fake_network
{
    string sig;
    int looked {0};
    string operator()() { ++looked; return sig; }
};

// A stream that only knows it was dropped.
struct fake_stream
{
    int drops {0};
    void drop() { ++drops; }
};
}

void test_r_network_watch::test_the_first_signature_is_not_a_change()
{
    r_network_watch w;
    fake_network n;
    n.sig = "en0=192.168.1.5/255.255.255.0;";
    for(int ms = 0; ms <= 10000; ms += 1000)
        RTF_ASSERT(!w.poll(at(ms), [&]{ return n(); }));
}

void test_r_network_watch::test_a_change_counts_once_it_has_held()
{
    // Wi-Fi to Ethernet: the new signature must stay the same for HOLD (3 s) before it is a change, and it is one once.
    r_network_watch w;
    fake_network n;
    n.sig = "en0=192.168.1.5/255.255.255.0;";
    RTF_ASSERT(!w.poll(at(0), [&]{ return n(); }));
    n.sig = "en7=192.168.1.9/255.255.255.0;";
    RTF_ASSERT(!w.poll(at(1000), [&]{ return n(); }));      // first seen
    RTF_ASSERT(!w.poll(at(2000), [&]{ return n(); }));      // held 1 s
    RTF_ASSERT(!w.poll(at(3000), [&]{ return n(); }));      // held 2 s
    RTF_ASSERT(w.poll(at(4000), [&]{ return n(); }));       // held 3 s
    RTF_ASSERT(!w.poll(at(5000), [&]{ return n(); }));
    n.sig = "en0=192.168.1.5/255.255.255.0;";
    RTF_ASSERT(!w.poll(at(6000), [&]{ return n(); }));
    RTF_ASSERT(!w.poll(at(8000), [&]{ return n(); }));
    RTF_ASSERT(w.poll(at(9000), [&]{ return n(); }));
}

void test_r_network_watch::test_a_flap_is_not_a_change()
{
    // An address that comes and goes, or a different one each time, never holds for HOLD.
    r_network_watch w;
    fake_network n;
    n.sig = "a";
    RTF_ASSERT(!w.poll(at(0), [&]{ return n(); }));
    int ms = 1000;
    for(auto s : {"b", "a", "b", "c", "a", "b", "c", "a"})
    {
        n.sig = s;
        RTF_ASSERT(!w.poll(at(ms), [&]{ return n(); }));
        ms += 1000;
    }
}

void test_r_network_watch::test_losing_and_regaining_the_network_are_changes()
{
    r_network_watch w;
    fake_network n;
    n.sig = "en0=10.0.0.2/255.0.0.0;";
    RTF_ASSERT(!w.poll(at(0), [&]{ return n(); }));
    n.sig = "";
    bool changed = false;
    for(int ms = 1000; ms <= 5000; ms += 1000)
        changed = changed || w.poll(at(ms), [&]{ return n(); });
    RTF_ASSERT(changed);                                    // no network at all
    n.sig = "en0=10.0.0.2/255.0.0.0;";
    changed = false;
    for(int ms = 6000; ms <= 10000; ms += 1000)
        changed = changed || w.poll(at(ms), [&]{ return n(); });
    RTF_ASSERT(changed);                                    // and back
}

void test_r_network_watch::test_polls_between_checks_do_not_look_at_the_network()
{
    // The keeper's loop wakes for every command: ten passes within a second are one look at the network.
    r_network_watch w;
    fake_network n;
    n.sig = "a";
    for(int ms = 0; ms < 1000; ms += 100)
        w.poll(at(ms), [&]{ return n(); });
    RTF_ASSERT_EQUAL(n.looked, 1);
    w.poll(at(1000), [&]{ return n(); });
    RTF_ASSERT_EQUAL(n.looked, 2);

    // And a change seen on two passes a few ms apart is not "held".
    n.sig = "b";
    RTF_ASSERT(!w.poll(at(2000), [&]{ return n(); }));
    RTF_ASSERT(!w.poll(at(2010), [&]{ return n(); }));
    RTF_ASSERT(!w.poll(at(2020), [&]{ return n(); }));
}

void test_r_network_watch::test_a_change_drops_every_stream_once()
{
    // The keeper's pass, with a fake network and fake streams: nothing is dropped while the network holds, every stream is
    // when it changes and has held, and not again after.
    map<string, shared_ptr<fake_stream>> streams;
    streams["a"] = make_shared<fake_stream>();
    streams["b"] = make_shared<fake_stream>();
    streams["c"] = make_shared<fake_stream>();

    r_network_watch w;
    fake_network n;
    n.sig = "en0=192.168.1.5/255.255.255.0;";
    size_t dropped = 0;
    for(int ms = 0; ms <= 5000; ms += 1000)
        dropped += r_drop_streams_on_network_change(w, at(ms), [&]{ return n(); }, streams);
    RTF_ASSERT_EQUAL(dropped, (size_t)0);
    for(auto& s : streams)
        RTF_ASSERT_EQUAL(s.second->drops, 0);

    n.sig = "en7=192.168.1.9/255.255.255.0;";
    for(int ms = 6000; ms <= 12000; ms += 1000)
        dropped += r_drop_streams_on_network_change(w, at(ms), [&]{ return n(); }, streams);
    RTF_ASSERT_EQUAL(dropped, (size_t)3);
    for(auto& s : streams)
        RTF_ASSERT_EQUAL(s.second->drops, 1);
}

void test_r_network_watch::test_vpn_and_vm_links_are_not_the_cameras_network()
{
    using r_utils::r_networking::r_adapter_info;
    using r_utils::r_networking::r_network_signature_of;
    auto adapter = [](const char* name, const char* ip, const char* mask){
        r_adapter_info a;
        a.name = name;
        a.ipv4_addr = ip;
        a.ipv4_netmask = mask;
        return a;
    };
    vector<r_adapter_info> lan = { adapter("en0", "192.168.68.87", "255.255.252.0") };
    auto base = r_network_signature_of(lan);
    RTF_ASSERT(base == "en0=192.168.68.87/255.255.252.0;");

    // A Mac with Tailscale and a running VM: its signature is the LAN's alone.
    vector<r_adapter_info> busy = lan;
    busy.push_back(adapter("utun6", "100.107.210.6", "255.255.255.255"));
    busy.push_back(adapter("bridge100", "192.168.64.1", "255.255.255.0"));
    busy.push_back(adapter("tailscale0", "100.64.0.2", "255.192.0.0"));
    busy.push_back(adapter("wg0", "10.9.0.2", "255.255.255.0"));
    busy.push_back(adapter("tun0", "10.8.0.6", "255.255.255.0"));
    busy.push_back(adapter("ppp0", "10.7.0.2", "255.255.255.0"));
    busy.push_back(adapter("vEthernet (WSL)", "172.20.0.1", "255.255.240.0"));
    busy.push_back(adapter("Tailscale", "100.64.0.3", "255.192.0.0"));
    busy.push_back(adapter("Ethernet 2", "10.1.1.1", "255.255.255.255"));
    RTF_ASSERT(r_network_signature_of(busy) == base);

    // Each skipped name on its own, with an ordinary netmask, so the name is what keeps it out (not the /32 rule).
    for(auto name : {"utun3", "tun0", "tap1", "wg0", "tailscale0", "ppp0", "ipsec0", "gif0", "stf0", "awdl0", "llw0", "bridge100",
                     "Tailscale", "WireGuard Tunnel", "OpenVPN TAP-Windows6", "vEthernet (Default Switch)"})
    {
        vector<r_adapter_info> with = lan;
        with.push_back(adapter(name, "10.50.0.2", "255.255.255.0"));
        RTF_ASSERT(r_network_signature_of(with) == base);
    }

    // A real second interface is a change.
    vector<r_adapter_info> two = lan;
    two.push_back(adapter("en7", "192.168.1.9", "255.255.255.0"));
    RTF_ASSERT(r_network_signature_of(two) != base);
    // Wi-Fi to Ethernet too (a Windows name).
    RTF_ASSERT(r_network_signature_of({adapter("Wi-Fi", "192.168.1.5", "255.255.255.0")}) !=
               r_network_signature_of({adapter("Ethernet", "192.168.1.9", "255.255.255.0")}));
}
