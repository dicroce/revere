#include "framework.h"

class test_r_record_from : public test_fixture
{
public:
    RTF_FIXTURE(test_r_record_from);
      TEST(test_r_record_from::test_update_properties_sets_record_from);
      TEST(test_r_record_from::test_the_substream_is_only_chosen_while_it_works);
    RTF_FIXTURE_END();

    virtual ~test_r_record_from() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_update_properties_sets_record_from();
    void test_the_substream_is_only_chosen_while_it_works();
};
