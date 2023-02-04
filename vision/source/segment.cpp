
#include "segment.h"

using namespace std;
using namespace vision;

int64_t vision::segment_to_millisecond_duration(const segment& s)
{
    return chrono::duration_cast<chrono::milliseconds>(s.end - s.start).count();
}
