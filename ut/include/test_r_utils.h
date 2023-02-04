
#include "framework.h"

class test_r_utils : public test_fixture
{
public:
    RTF_FIXTURE(test_r_utils);
      TEST(test_r_utils::test_string_utils_split);
      TEST(test_r_utils::test_string_utils_basic_format);
      TEST(test_r_utils::test_string_utils_basic_format_buffer);
      TEST(test_r_utils::test_string_utils_numeric_conversion);
      TEST(test_r_utils::test_string_utils_base64_encode);
      TEST(test_r_utils::test_string_utils_base64_decode);
      TEST(test_r_utils::test_stack_trace);
      TEST(test_r_utils::test_stack_trace_exception);
      TEST(test_r_utils::test_read_file);
      TEST(test_r_utils::test_stat);
      TEST(test_r_utils::test_file_lock);
      TEST(test_r_utils::test_shared_file_lock);
      TEST(test_r_utils::test_md5_basic);
      TEST(test_r_utils::test_client_server);
      TEST(test_r_utils::test_socket_move_constructable);
      TEST(test_r_utils::test_socket_move_assignable);
      TEST(test_r_utils::test_socket_close_warm_socket);
      TEST(test_r_utils::test_socket_wont_block);
      TEST(test_r_utils::test_buffered);
      TEST(test_r_utils::test_udp_send);
      TEST(test_r_utils::test_udp_associated_send);
      TEST(test_r_utils::test_udp_get_set_recv_buffer_size);
      TEST(test_r_utils::test_udp_test_shutdown_closed_while_blocked);
      TEST(test_r_utils::test_udp_raw_recv);
      TEST(test_r_utils::test_udp_shutdown_close_while_blocked_in_raw_recv);
      TEST(test_r_utils::test_8601_to_tp);
      TEST(test_r_utils::test_tp_to_8601);
      TEST(test_r_utils::test_uuid_generate);
      TEST(test_r_utils::test_uuid_to_s);
      TEST(test_r_utils::test_s_to_uuid);
      TEST(test_r_utils::test_blob_tree_basic);
      TEST(test_r_utils::test_blob_tree_objects_in_array);
      TEST(test_r_utils::test_work_q_basic);
      TEST(test_r_utils::test_work_q_timeout);
      TEST(test_r_utils::test_timer_basic);
      TEST(test_r_utils::test_sha1_basic);
      TEST(test_r_utils::test_exp_avg);
    RTF_FIXTURE_END();

    virtual ~test_r_utils() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_string_utils_split();
    void test_stack_trace();
    void test_stack_trace_exception();
    void test_string_utils_basic_format();
    void test_string_utils_basic_format_buffer();
    void test_string_utils_numeric_conversion();
    void test_string_utils_base64_encode();
    void test_string_utils_base64_decode();
    void test_read_file();
    void test_stat();
    void test_file_lock();
    void test_shared_file_lock();
    void test_md5_basic();
    void test_client_server();
    void test_socket_move_constructable();
    void test_socket_move_assignable();
    void test_socket_close_warm_socket();
    void test_socket_wont_block();
    void test_buffered();
    void test_udp_send();
    void test_udp_associated_send();
    void test_udp_get_set_recv_buffer_size();
    void test_udp_test_shutdown_closed_while_blocked();
    void test_udp_raw_recv();
    void test_udp_shutdown_close_while_blocked_in_raw_recv();
    void test_8601_to_tp();
    void test_tp_to_8601();
    void test_uuid_generate();
    void test_uuid_to_s();
    void test_s_to_uuid();
    void test_blob_tree_basic();
    void test_blob_tree_objects_in_array();
    void test_work_q_basic();
    void test_work_q_timeout();
    void test_timer_basic();
    void test_sha1_basic();
    void test_exp_avg();
};
