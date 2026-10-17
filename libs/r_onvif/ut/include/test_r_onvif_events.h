
#include "framework.h"

// Offline tests of the ONVIF events parsing: no camera or network needed.
class test_r_onvif_events : public test_fixture
{
public:
    RTF_FIXTURE(test_r_onvif_events);
      TEST(test_r_onvif_events::test_pull_point_without_reference_parameters);
      TEST(test_r_onvif_events::test_pull_point_reference_parameters_keep_namespaces);
      TEST(test_r_onvif_events::test_pull_messages);
      TEST(test_r_onvif_events::test_pull_messages_empty);
      TEST(test_r_onvif_events::test_event_topics);
      TEST(test_r_onvif_events::test_unparseable_responses_throw);
    RTF_FIXTURE_END();

    virtual ~test_r_onvif_events() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_pull_point_without_reference_parameters();
    void test_pull_point_reference_parameters_keep_namespaces();
    void test_pull_messages();
    void test_pull_messages_empty();
    void test_event_topics();
    void test_unparseable_responses_throw();
};
