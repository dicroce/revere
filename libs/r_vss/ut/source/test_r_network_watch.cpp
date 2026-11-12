#include "test_r_network_watch.h"
#include "r_vss/r_network_watch.h"

using namespace std;
using namespace r_vss;

REGISTER_TEST_FIXTURE(test_r_network_watch);

void test_r_network_watch::test_the_first_signature_is_not_a_change()
{
    r_network_watch w;
    RTF_ASSERT(!w.poll("en0=192.168.1.5/255.255.255.0;"));
    RTF_ASSERT(!w.poll("en0=192.168.1.5/255.255.255.0;"));
    RTF_ASSERT(!w.poll("en0=192.168.1.5/255.255.255.0;"));
}

void test_r_network_watch::test_a_change_counts_once_it_holds()
{
    // Wi-Fi to Ethernet: the new signature is seen on two passes, then it is a change, once.
    r_network_watch w;
    RTF_ASSERT(!w.poll("en0=192.168.1.5/255.255.255.0;"));
    RTF_ASSERT(!w.poll("en7=192.168.1.9/255.255.255.0;"));
    RTF_ASSERT(w.poll("en7=192.168.1.9/255.255.255.0;"));
    RTF_ASSERT(!w.poll("en7=192.168.1.9/255.255.255.0;"));
    // And the next change counts the same way.
    RTF_ASSERT(!w.poll("en0=192.168.1.5/255.255.255.0;"));
    RTF_ASSERT(w.poll("en0=192.168.1.5/255.255.255.0;"));
}

void test_r_network_watch::test_a_flap_is_not_a_change()
{
    // An address that comes and goes between passes, or a different one each pass, settles on nothing new.
    r_network_watch w;
    RTF_ASSERT(!w.poll("a"));
    RTF_ASSERT(!w.poll("b"));
    RTF_ASSERT(!w.poll("a"));       // back to what it was
    RTF_ASSERT(!w.poll("b"));
    RTF_ASSERT(!w.poll("c"));       // a third value: still not seen twice
    RTF_ASSERT(!w.poll("a"));
}

void test_r_network_watch::test_losing_and_regaining_the_network_are_changes()
{
    r_network_watch w;
    RTF_ASSERT(!w.poll("en0=10.0.0.2/255.0.0.0;"));
    RTF_ASSERT(!w.poll(""));
    RTF_ASSERT(w.poll(""));                                   // no network at all
    RTF_ASSERT(!w.poll("en0=10.0.0.2/255.0.0.0;"));
    RTF_ASSERT(w.poll("en0=10.0.0.2/255.0.0.0;"));            // and back
}
