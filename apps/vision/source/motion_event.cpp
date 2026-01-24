
#include "motion_event.h"

using namespace std;
using namespace vision;

int64_t vision::motion_event_to_millisecond_duration(const motion_event& me)
{
    return chrono::duration_cast<chrono::milliseconds>(me.end - me.start).count();
}
