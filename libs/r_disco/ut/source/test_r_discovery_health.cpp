#include "test_r_discovery_health.h"
#include "r_disco/r_discovery_health.h"
#include <string>

using namespace r_disco;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_discovery_health);

void test_r_discovery_health::test_no_cameras_never_warns()
{
    // A network with no cameras (or with them all off): nothing answers anything, pass after pass. Not a firewall.
    r_discovery_health h;
    for(int i = 0; i < 100; ++i)
        RTF_ASSERT(!h.pass(0, 0));
    RTF_ASSERT(!h.replies_blocked());
    RTF_ASSERT(h.warning().empty());
}

void test_r_discovery_health::test_only_direct_answers_warn_after_three_passes()
{
    // ufw on Linux dropping the replies to the multicast probe: the recorded cameras answer the direct probes of
    // their addresses, and no camera answers multicast. A pass or two of that can be a lost reply; three is the sign.
    r_discovery_health h;
    RTF_ASSERT_EQUAL((int)r_discovery_health::PASSES_TO_WARN, 3);
    RTF_ASSERT(!h.pass(0, 2));
    RTF_ASSERT(!h.pass(0, 2));
    RTF_ASSERT(!h.replies_blocked());
    RTF_ASSERT(h.pass(0, 2));            // changed
    RTF_ASSERT(h.replies_blocked());
    RTF_ASSERT(!h.pass(0, 1));           // still blocked: no change
    RTF_ASSERT(h.replies_blocked());

    auto w = h.warning();
    RTF_ASSERT_EQUAL(w, string(r_discovery_health::warning_text()));
    RTF_ASSERT(w.find("firewall") != string::npos);
    RTF_ASSERT(w.find("UDP from port 3702") != string::npos);
}

void test_r_discovery_health::test_a_multicast_answer_clears_it()
{
    // The firewall rule added: the next multicast answer clears it at once, and it takes three passes again to
    // come back.
    r_discovery_health h;
    for(int i = 0; i < 3; ++i)
        h.pass(0, 1);
    RTF_ASSERT(h.replies_blocked());
    RTF_ASSERT(h.pass(1, 0));            // changed
    RTF_ASSERT(!h.replies_blocked());
    RTF_ASSERT(h.warning().empty());
    RTF_ASSERT(!h.pass(0, 1));
    RTF_ASSERT(!h.pass(0, 1));
    RTF_ASSERT(h.pass(0, 1));
    RTF_ASSERT(h.replies_blocked());

    // A multicast answer between suspect passes starts the count over.
    r_discovery_health g;
    g.pass(0, 1);
    g.pass(0, 1);
    g.pass(2, 0);
    g.pass(0, 1);
    g.pass(0, 1);
    RTF_ASSERT(!g.replies_blocked());
}

void test_r_discovery_health::test_silent_passes_say_nothing()
{
    // A pass where nothing answered (the cameras off for a while) neither counts toward the warning nor clears it.
    r_discovery_health h;
    h.pass(0, 1);
    h.pass(0, 1);
    h.pass(0, 0);
    RTF_ASSERT(!h.replies_blocked());
    RTF_ASSERT(h.pass(0, 1));
    RTF_ASSERT(h.replies_blocked());
    RTF_ASSERT(!h.pass(0, 0));
    RTF_ASSERT(h.replies_blocked());
}

void test_r_discovery_health::test_some_cameras_ignoring_multicast_is_no_warning()
{
    // Some cheap cameras ignore multicast Probes and are found only by direct probes. While any other camera
    // answers multicast, replies get through: no warning.
    r_discovery_health h;
    for(int i = 0; i < 10; ++i)
        RTF_ASSERT(!h.pass(3, 2));
    RTF_ASSERT(!h.replies_blocked());
}
