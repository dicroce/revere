
#include "test_r_motion.h"
#include "r_motion/r_motion_state.h"
#include <algorithm>

using namespace std;
using namespace r_motion;

REGISTER_TEST_FIXTURE(test_r_motion);

void test_r_motion::setup()
{
}

void test_r_motion::teardown()
{
}

void test_r_motion::test_motion_state()
{
    const int width = 640, height = 360;
    cv::Mat background(height, width, CV_8UC3, cv::Scalar(200, 200, 200));
    r_motion_state ms(60, 0.95, 0.70, 100, false, 0.003);
    for(int i = 0; i < 10; ++i)
        ms.process(background, 0, 0, false, i * 1000);

    cv::Mat small = background.clone();
    cv::rectangle(small, cv::Rect(100, 100, 15, 30), cv::Scalar(255, 255, 255), cv::FILLED);
    auto small_result = ms.process(small, 0, 0, false, 10000);
    RTF_ASSERT(!small_result.is_null());
    RTF_ASSERT(small_result.value().motion > 0);
    RTF_ASSERT(small_result.value().motion_bbox.has_motion);

    r_motion_state fragmented_ms(60, 0.95, 0.70, 100, false, 0.003);
    for(int i = 0; i < 10; ++i)
        fragmented_ms.process(background, 0, 0, false, i * 1000);
    cv::Mat fragmented = background.clone();
    cv::rectangle(fragmented, cv::Rect(100, 100, 20, 25), cv::Scalar(255, 255, 255), cv::FILLED);
    cv::rectangle(fragmented, cv::Rect(180, 100, 20, 25), cv::Scalar(255, 255, 255), cv::FILLED);
    auto fragmented_result = fragmented_ms.process(fragmented, 0, 0, false, 10000);
    RTF_ASSERT(!fragmented_result.is_null());
    RTF_ASSERT(fragmented_result.value().motion_regions.size() == 2);
    RTF_ASSERT(fragmented_result.value().motion > 0);
}

void test_r_motion::test_adaptive_masking()
{
    cv::Mat frame(240, 320, CV_8UC3, cv::Scalar(100, 100, 100));
    r_motion_state ms(60, 0.30, 0.80, 5, true, 0.003);
    r_utils::r_nullable<r_motion_info> last;
    for(int i = 0; i < 15; ++i)
    {
        frame.setTo(cv::Scalar(i % 2 ? 180 : 100, i % 2 ? 180 : 100, i % 2 ? 180 : 100));
        last = ms.process(frame, 0, 0, false, i * 1000);
    }
    RTF_ASSERT(!last.is_null());
    RTF_ASSERT(last.value().masking_active);
}

void test_r_motion::test_shadow_evidence()
{
    cv::Mat background(360, 640, CV_8UC3, cv::Scalar(200, 200, 200));
    r_motion_state ms(60, 0.95, 0.70, 100, false, 0.003);
    for(int i = 0; i < 10; ++i)
        ms.process(background, 0, 0, false, i * 1000);

    cv::Mat darker = background.clone();
    cv::rectangle(darker, cv::Rect(100, 80, 100, 100), cv::Scalar(120, 120, 120), cv::FILLED);
    auto result = ms.process(darker, 0, 0, false, 10000);
    RTF_ASSERT(!result.is_null());
    RTF_ASSERT(result.value().weak_motion > 0);
    RTF_ASSERT(result.value().motion > 0);
}

void test_r_motion::test_illumination_and_large_object()
{
    cv::Mat background(360, 640, CV_8UC3, cv::Scalar(200, 200, 200));

    r_motion_state object_ms(60, 0.95, 0.70, 100, false, 0.003);
    for(int i = 0; i < 10; ++i)
        object_ms.process(background, 0, 0, false, i * 1000);
    cv::Mat large_object = background.clone();
    cv::rectangle(large_object, cv::Rect(0, 0, 640, 100), cv::Scalar(0, 0, 0), cv::FILLED);
    auto object_result = object_ms.process(large_object, 0, 0, false, 10000);
    RTF_ASSERT(!object_result.is_null());
    RTF_ASSERT(object_result.value().motion > 0);
    RTF_ASSERT(!object_result.value().illumination_change);

    r_motion_state light_ms(60, 0.95, 0.70, 100, false, 0.003);
    for(int i = 0; i < 10; ++i)
        light_ms.process(background, 0, 0, false, i * 1000);
    cv::Mat brighter(360, 640, CV_8UC3, cv::Scalar(230, 230, 230));
    auto light_result = light_ms.process(brighter, 0, 0, false, 10000);
    RTF_ASSERT(!light_result.is_null());
    RTF_ASSERT(light_result.value().illumination_change);
    RTF_ASSERT(light_result.value().illumination_motion > 0);
    RTF_ASSERT(light_result.value().motion > 0);
}

void test_r_motion::test_image_formats()
{
    const int width = 320, height = 240;
    r_image argb;
    argb.type = R_MOTION_IMAGE_TYPE_ARGB;
    argb.width = width;
    argb.height = height;
    argb.data.resize(width * height * 4, 0);
    for(int i = 0; i < width * height; ++i)
        argb.data[i * 4] = 255;

    r_motion_state argb_ms(60, 0.95, 0.70, 100, false, 0.003);
    for(int i = 0; i < 10; ++i)
        argb_ms.process(argb, false, i * 1000);
    for(int y = 50; y < 150; ++y)
        for(int x = 50; x < 150; ++x)
            argb.data[(y * width + x) * 4 + 3] = 255; // blue in A,R,G,B
    auto argb_result = argb_ms.process(argb, false, 10000);
    RTF_ASSERT(!argb_result.is_null());
    RTF_ASSERT(argb_result.value().motion > 0);

    r_image gray;
    gray.type = R_MOTION_IMAGE_TYPE_GRAY8;
    gray.width = width;
    gray.height = height;
    gray.data.assign(width * height, 100);
    r_motion_state gray_ms(60, 0.95, 0.70, 100, false, 0.003);
    for(int i = 0; i < 10; ++i)
        gray_ms.process(gray, false, i * 1000);
    std::fill(gray.data.begin() + 50 * width + 50,
              gray.data.begin() + 50 * width + 150, 200);
    auto gray_result = gray_ms.process(gray, false, 10000);
    RTF_ASSERT(!gray_result.is_null());
}
