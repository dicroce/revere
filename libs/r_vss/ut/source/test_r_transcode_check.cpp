#include "test_r_transcode_check.h"
#include "r_vss/r_query.h"

using namespace r_vss;

REGISTER_TEST_FIXTURE(test_r_transcode_check);

void test_r_transcode_check::test_mostly_failed()
{
    // The export after a GPU fault: about 62,000 decode failures in a 30-minute clip, so almost nothing decoded.
    RTF_ASSERT(transcode_mostly_failed(54000, 10));
    RTF_ASSERT(transcode_mostly_failed(300, 0));
    // Fewer than half.
    RTF_ASSERT(transcode_mostly_failed(300, 149));
    RTF_ASSERT(!transcode_mostly_failed(300, 150));
    // A chunk that decodes, less the frames a decoder holds back or the ones before its first key frame.
    RTF_ASSERT(!transcode_mostly_failed(300, 290));
    RTF_ASSERT(!transcode_mostly_failed(300, 300));
    // Too few frames to say: a short chunk, or one with no video.
    RTF_ASSERT(!transcode_mostly_failed(TRANSCODE_CHECK_MIN_FRAMES - 1, 0));
    RTF_ASSERT(transcode_mostly_failed(TRANSCODE_CHECK_MIN_FRAMES, 0));
    RTF_ASSERT(!transcode_mostly_failed(0, 0));
}
