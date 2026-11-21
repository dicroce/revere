
#ifndef __r_motion_event_h
#define __r_motion_event_h

namespace r_vss
{

enum r_motion_event
{
    motion_event_start,
    motion_event_update,
    motion_event_end,
    // An event that carries straight on from the camera's previous one: the motion went on past the longest an event
    // runs (r_motion_engine's max event length), so that one was ended (motion_event_end, at this same time and frame)
    // and this one starts where it stopped. Everything a start means, plus "the same motion as before": the events
    // since the last motion_event_start are one chain.
    motion_event_continue
};

}

#endif
