#include "r_vss/r_restream_track.h"
#include "r_vss/r_recording_context.h"
#include "r_utils/r_string_utils.h"

#ifdef IS_LINUX
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <gst/gst.h>
#ifdef IS_LINUX
#pragma GCC diagnostic pop
#endif

using namespace r_vss;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

static int64_t _ns(steady_clock::time_point tp)
{
    // Never 0, which means "never pulled".
    auto ns = duration_cast<nanoseconds>(tp.time_since_epoch()).count();
    return ns != 0 ? ns : 1;
}

void r_restream_track_gate::begin_pull(steady_clock::time_point now)
{
    _last_pull_ns = _ns(now);
    _waiting = true;
}

void r_restream_track_gate::end_pull(steady_clock::time_point now)
{
    _last_pull_ns = _ns(now);
    _waiting = false;
}

bool r_restream_track_gate::wanted(steady_clock::time_point now, steady_clock::duration idle) const
{
    if(_waiting)
        return true;
    auto last = _last_pull_ns.load();
    if(last == 0)
        return false;
    return _ns(now) - last < duration_cast<nanoseconds>(idle).count();
}

bool r_restream_track_gate::breaks(int64_t pts_ns, int64_t duration_ns)
{
    bool brk = _next_pts_ns < 0 || pts_ns > _next_pts_ns + RESTREAM_DISCONT_NS || pts_ns < _next_pts_ns - RESTREAM_DISCONT_NS;
    _next_pts_ns = duration_ns >= 0 ? pts_ns + duration_ns : pts_ns;
    return brk;
}

void r_vss::restream_feed(GstElement* appsrc, r_blocking_q<_frame_context>& q, r_restream_track_gate& gate, bool audio)
{
    gate.begin_pull();

    GstPad* pad = gst_element_get_static_pad(appsrc, "src");
    r_nullable<_frame_context> sample;
    while(true)
    {
        sample = q.poll(milliseconds(250));
        if(!sample.is_null())
            break;
        // Deactivating the pad (session teardown) marks it flushing before it
        // waits for this streaming thread: stop waiting then.
        if(!pad || GST_PAD_IS_FLUSHING(pad))
            break;
    }
    if(pad)
        gst_object_unref(pad);

    if(!sample.is_null())
    {
        GstBuffer* output_buffer = gst_buffer_copy(sample.value().buffer.get());
        GST_BUFFER_PTS(output_buffer) = sample.value().gst_pts;
        GST_BUFFER_DTS(output_buffer) = sample.value().gst_dts;
        // Audio: the camera's buffer brings its own flags; a discontinuity only where this track's times really break.
        if(audio)
        {
            auto duration = GST_BUFFER_DURATION_IS_VALID(output_buffer) ? (int64_t)GST_BUFFER_DURATION(output_buffer) : (int64_t)-1;
            if(gate.breaks((int64_t)sample.value().gst_pts, duration))
                GST_BUFFER_FLAG_SET(output_buffer, GST_BUFFER_FLAG_DISCONT);
            else
                GST_BUFFER_FLAG_UNSET(output_buffer, GST_BUFFER_FLAG_DISCONT | GST_BUFFER_FLAG_RESYNC);
        }
        int ret;
        g_signal_emit_by_name(appsrc, "push-buffer", output_buffer, &ret);
        gst_buffer_unref(output_buffer);
    }

    gate.end_pull();
}

string r_vss::format_restream_overflow(const vector<restream_overflow>& sessions, int seconds)
{
    string list;
    for(const auto& s : sessions)
    {
        if(s.video_dropped == 0 && s.audio_dropped == 0)
            continue;
        string tracks;
        if(s.video_dropped > 0)
            tracks += r_string_utils::format("video %zu", s.video_dropped);
        if(s.audio_dropped > 0)
            tracks += r_string_utils::format("%saudio %zu", tracks.empty() ? "" : ", ", s.audio_dropped);
        list += r_string_utils::format("%s%s session %llu %s", list.empty() ? "" : "; ", s.camera.c_str(), (unsigned long long)s.session, tracks.c_str());
    }
    if(list.empty())
        return string();
    return r_string_utils::format("Live restream overflow (frames dropped in the last %d s): ", seconds) + list;
}
