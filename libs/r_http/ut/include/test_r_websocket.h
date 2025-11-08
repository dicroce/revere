
#include "framework.h"

class test_r_websocket : public test_fixture
{
public:
    RTF_FIXTURE(test_r_websocket);
      TEST(test_r_websocket::test_frame_build_text);
      TEST(test_r_websocket::test_frame_build_binary);
      TEST(test_r_websocket::test_frame_build_control);
      TEST(test_r_websocket::test_frame_parse_text);
      TEST(test_r_websocket::test_frame_parse_binary);
      TEST(test_r_websocket::test_frame_parse_masked);
      TEST(test_r_websocket::test_frame_build_extended_16bit);
      TEST(test_r_websocket::test_frame_build_extended_64bit);
      TEST(test_r_websocket::test_compute_accept_key);
      TEST(test_r_websocket::test_generate_websocket_key);
      TEST(test_r_websocket::test_client_server_text_exchange);
      TEST(test_r_websocket::test_client_server_binary_exchange);
      TEST(test_r_websocket::test_client_server_ping_pong);
      TEST(test_r_websocket::test_client_server_close_handshake);
      TEST(test_r_websocket::test_full_http_upgrade_handshake);
      TEST(test_r_websocket::test_websocket_client_wrapper);
    RTF_FIXTURE_END();

    virtual ~test_r_websocket() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_frame_build_text();
    void test_frame_build_binary();
    void test_frame_build_control();
    void test_frame_parse_text();
    void test_frame_parse_binary();
    void test_frame_parse_masked();
    void test_frame_build_extended_16bit();
    void test_frame_build_extended_64bit();
    void test_compute_accept_key();
    void test_generate_websocket_key();
    void test_client_server_text_exchange();
    void test_client_server_binary_exchange();
    void test_client_server_ping_pong();
    void test_client_server_close_handshake();
    void test_full_http_upgrade_handshake();
    void test_websocket_client_wrapper();
};
