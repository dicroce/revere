#include "framework.h"

// Offline: Reolink API parsing, polled-state tracking, and the ONVIF ->
// Reolink fallback chain with fake sources. No camera or network needed.
class test_r_reolink : public test_fixture
{
public:
    RTF_FIXTURE(test_r_reolink);
      TEST(test_r_reolink::test_parse_state);
      TEST(test_r_reolink::test_state_tracker);
      TEST(test_r_reolink::test_chain_falls_back_and_returns);
      TEST(test_r_reolink::test_chain_probe_fails);
    RTF_FIXTURE_END();

    virtual ~test_r_reolink() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_parse_state();
    void test_state_tracker();
    void test_chain_falls_back_and_returns();
    void test_chain_probe_fails();
};
