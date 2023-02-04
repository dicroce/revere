
#ifndef __vision_control_bar_h
#define __vision_control_bar_h

#include <map>
#include <functional>
#include <mutex>
#include <string>
#include <cstdint>
#include <algorithm>
#include <chrono>
#include "timerange.h"
#include "segment.h"
#include "motion_event.h"

namespace vision
{

enum control_bar_button_type
{
    CONTROL_BAR_BUTTON_LIVE
};

enum export_state
{
    EXPORT_STATE_NONE,
    EXPORT_STATE_CONFIGURING,
    EXPORT_STATE_STARTED,
    EXPORT_STATE_FINISHED_SUCCESS,
    EXPORT_STATE_FINISHED_ERROR
};

struct control_bar_state
{
    int playhead_pos {1000};
    uint16_t playhead_x {4};
    uint16_t playhead_width {3};
    bool dragging {false};
    uint16_t timerange_minutes {30};
    timerange tr;
    std::vector<segment> segments;
    std::vector<motion_event> motion_events;
    bool entered {false};
    bool need_update_data {true};
    export_state exp_state {EXPORT_STATE_NONE};
    std::chrono::system_clock::time_point export_start_time;

    // Set the number of minutes in the timerange.
    // This is centered around the current playhead position.
    // Get the current playhead position in time, and set the playhead to it after the range is set if it is within the new range.
    void set_timerange_minutes(uint16_t minutes)
    {
        if(minutes < 10)
            minutes = 10;

        auto playhead_entry_frac = playhead_pos / 1000.0;
        auto playhead_time = tr.get_start() + std::chrono::seconds((int)(playhead_entry_frac * std::chrono::duration_cast<std::chrono::seconds>(tr.get_end() - tr.get_start()).count()));

        // Use the current timerange to find the TIME of the playhead
        // After setting the range, set the playhead to the same time

        auto half = minutes / 2;
        timerange_minutes = minutes;

        if(minutes >= timerange_minutes)
            set_range(playhead_time + std::chrono::minutes(half));
        else
            set_range(playhead_time - std::chrono::minutes(half));

        auto new_time_delta = playhead_time - tr.get_start();
        auto new_time_frac = std::chrono::duration_cast<std::chrono::seconds>(new_time_delta).count() / (double)std::chrono::duration_cast<std::chrono::seconds>(tr.get_end() - tr.get_start()).count();
        playhead_pos = (int)(new_time_frac * 1000);
    }

    uint16_t get_timerange_minutes() const
    {
        return timerange_minutes;
    }

    void set_range(const std::chrono::system_clock::time_point& end)
    {
        auto now = std::chrono::system_clock::now();
        if(end > now)
        {
            tr.set_end(now);
            tr.set_start(now - std::chrono::minutes(timerange_minutes));
        }
        else
        {
            tr.set_start(end - std::chrono::minutes(timerange_minutes));
            tr.set_end(end);

        }
        //playhead_pos = 1000;
        need_update_data = true;
    }

    void live()
    {
        set_range(std::chrono::system_clock::now());
    }

    std::pair<std::chrono::system_clock::time_point, std::chrono::system_clock::time_point> get_range() const
    {
        return tr.get();
    }

    // Move the bar forward in time by the specified duration.
    void forward(const std::chrono::seconds& d)
    {
        auto now = std::chrono::system_clock::now();

        // Remember the current playhead position in time
        auto playhead_entry_frac = playhead_pos / 1000.0;
        auto playhead_time = tr.get_start() + std::chrono::seconds((int)(playhead_entry_frac * std::chrono::duration_cast<std::chrono::seconds>(tr.get_end() - tr.get_start()).count()));

        // Move, but dont move past the current time.
        auto original_end = tr.get_end();
        auto then = original_end + d;
        if(then > now)
        {
            tr.set_end(now);
            tr.set_start(tr.get_start() + (now - original_end));
        }
        else
        {
            tr.set_end(then);
            tr.set_start(tr.get_start() + d);
        }

        // If the old playhead time is outside the new range, move it to the start or end of the range
        if(playhead_time < tr.get_start())
            playhead_time = tr.get_start();
        if(playhead_time > tr.get_end())
            playhead_time = tr.get_end();

        // Set the playhead position to the same time it was before the move
        auto new_time_delta = playhead_time - tr.get_start();
        auto new_time_frac = std::chrono::duration_cast<std::chrono::seconds>(new_time_delta).count() / (double)std::chrono::duration_cast<std::chrono::seconds>(tr.get_end() - tr.get_start()).count();
        playhead_pos = (int)(new_time_frac * 1000);
    }

    // Move the bar backward in time by the specified duration.
    void backward(const std::chrono::seconds& d)
    {
        // Remember the current playhead position in time
        auto playhead_entry_frac = playhead_pos / 1000.0;
        auto playhead_time = tr.get_start() + std::chrono::seconds((int)(playhead_entry_frac * std::chrono::duration_cast<std::chrono::seconds>(tr.get_end() - tr.get_start()).count()));

        tr.set_start(tr.get_start() - d);
        tr.set_end(tr.get_end() - d);

        // If the old playhead time is outside the new range, move it to the start or end of the range
        if(playhead_time < tr.get_start())
            playhead_time = tr.get_start();
        if(playhead_time > tr.get_end())
            playhead_time = tr.get_end();

        // Set the playhead position to the same time it was before the move
        auto new_time_delta = playhead_time - tr.get_start();
        auto new_time_frac = std::chrono::duration_cast<std::chrono::seconds>(new_time_delta).count() / (double)std::chrono::duration_cast<std::chrono::seconds>(tr.get_end() - tr.get_start()).count();
        playhead_pos = (int)(new_time_frac * 1000);
    }

    std::string playhead_pos_s() const
    {
        auto delta = std::chrono::duration_cast<std::chrono::seconds>(tr.get_end() - tr.get_start()).count();
        auto pos = tr.get_start() + std::chrono::seconds((int)(delta * (playhead_pos / 1000.0)));
        return _tp_to_s(pos);
    }

    std::string range_start_s() const {return _tp_to_s(tr.get_start());}

    std::string range_end_s() const {return _tp_to_s(tr.get_end());}

    void set_contents(const std::vector<segment>& segs) {segments = segs;}

    void set_motion_events(const std::vector<motion_event>& events) {motion_events = events;}

private:
    std::string _tp_to_s(const std::chrono::system_clock::time_point& tp) const
    {
        auto t = std::chrono::system_clock::to_time_t(tp);
        char buf[64];
#ifdef IS_WINDOWS
        struct tm bdt;
        localtime_s(&bdt, &t);
        std::strftime(buf, 64, "%b %d, %Y\n%r", &bdt);
#endif
#ifdef IS_LINUX
        auto bdt = std::localtime(&t);
        std::strftime(buf, 64, "%b %d, %Y\n%r", bdt);
#endif
        return std::string(buf);
    }
};

}

#endif
