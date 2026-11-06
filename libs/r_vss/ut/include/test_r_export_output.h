#include "framework.h"

class test_r_export_output : public test_fixture
{
public:
    RTF_FIXTURE(test_r_export_output);
      TEST(test_r_export_output::test_progress_stops_short_of_100);
      TEST(test_r_export_output::test_chunk_repeats_left_out);
      TEST(test_r_export_output::test_nothing_written_fails);
      TEST(test_r_export_output::test_stopped_fails);
      TEST(test_r_export_output::test_written_is_renamed_into_place);
      TEST(test_r_export_output::test_unfinished_leaves_no_temp_file);
    RTF_FIXTURE_END();

    virtual ~test_r_export_output() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_progress_stops_short_of_100();
    void test_chunk_repeats_left_out();
    void test_nothing_written_fails();
    void test_stopped_fails();
    void test_written_is_renamed_into_place();
    void test_unfinished_leaves_no_temp_file();
};
