#include "framework.h"

class test_r_rtsp_transport : public test_fixture
{
public:
    RTF_FIXTURE(test_r_rtsp_transport);
      TEST(test_r_rtsp_transport::test_starts_on_tcp);
      TEST(test_r_rtsp_transport::test_unreachable_camera_stays_on_tcp);
      TEST(test_r_rtsp_transport::test_tcp_refused_falls_back_to_udp);
      TEST(test_r_rtsp_transport::test_working_transport_is_kept);
      TEST(test_r_rtsp_transport::test_neither_working_alternates);
      TEST(test_r_rtsp_transport::test_rtsp_transport_saved);
      TEST(test_r_rtsp_transport::test_rtsp_transport_upgrade_keeps_cameras);
    RTF_FIXTURE_END();

    virtual ~test_r_rtsp_transport() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_starts_on_tcp();
    void test_unreachable_camera_stays_on_tcp();
    void test_tcp_refused_falls_back_to_udp();
    void test_working_transport_is_kept();
    void test_neither_working_alternates();
    void test_rtsp_transport_saved();
    void test_rtsp_transport_upgrade_keeps_cameras();
};
