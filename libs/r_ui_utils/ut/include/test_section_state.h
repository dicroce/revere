#include "framework.h"

class test_section_state : public test_fixture
{
public:
    RTF_FIXTURE(test_section_state);
      TEST(test_section_state::test_round_trip);
      TEST(test_section_state::test_nothing_saved);
      TEST(test_section_state::test_bad_entries_skipped);
      TEST(test_section_state::test_not_an_object);
      TEST(test_section_state::test_from_file_text);
    RTF_FIXTURE_END();

    virtual ~test_section_state() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_round_trip();
    void test_nothing_saved();
    void test_bad_entries_skipped();
    void test_not_an_object();
    void test_from_file_text();
};
