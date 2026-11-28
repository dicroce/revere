
#include "framework.h"

class test_r_mux : public test_fixture
{
public:
    RTF_FIXTURE(test_r_mux);
      TEST(test_r_mux::test_basic_demux);
      TEST(test_r_mux::test_basic_mux);
      TEST(test_r_mux::test_fragmented_buffer_mux);
      TEST(test_r_mux::test_fragmented_transcode_window);
      TEST(test_r_mux::test_a_failed_header_closes_the_output_file);
      TEST(test_r_mux::test_a_failed_header_frees_the_buffer_output);
      TEST(test_r_mux::test_a_failed_header_can_be_opened_again);
    RTF_FIXTURE_END();

    virtual ~test_r_mux() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_basic_demux();
    void test_basic_mux();
    void test_fragmented_buffer_mux();
    void test_fragmented_transcode_window();
    void test_a_failed_header_closes_the_output_file();
    void test_a_failed_header_frees_the_buffer_output();
    void test_a_failed_header_can_be_opened_again();
};
