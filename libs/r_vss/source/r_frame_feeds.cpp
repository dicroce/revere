
#include "r_vss/r_frame_feeds.h"
#include <chrono>

using namespace r_vss;
using namespace std;

uint64_t r_frame_feeds::subscribe(const string& camera_id, const string& stream, uint32_t media, size_t max_bytes)
{
    if(media == 0 || (media & ~(uint32_t)(VIDEO | AUDIO)) != 0)
        return 0;
    if(!stream.empty() && stream != "main" && stream != "sub")
        return 0;

    auto f = make_shared<_feed>();
    f->camera_id = camera_id;
    f->media = media;
    f->max_bytes = max_bytes ? max_bytes : DEFAULT_MAX_BYTES;

    lock_guard<mutex> g(_lock);
    auto id = _next_id++;
    _feeds[id] = f;
    _count = _feeds.size();
    return id;
}

int r_frame_feeds::next(uint64_t feed, int timeout_ms, r_feed_frame& frame)
{
    unique_lock<mutex> g(_lock);
    auto it = _feeds.find(feed);
    if(it == _feeds.end())
        return -1;
    // Held while waiting: unsubscribe() can drop it from the map meanwhile.
    auto f = it->second;

    auto ready = [&f]() { return f->closed || !f->q.empty(); };
    if(!f->cond.wait_for(g, chrono::milliseconds(timeout_ms < 0 ? 0 : timeout_ms), ready))
        return 0;
    if(f->closed)
        return -1;

    frame = std::move(f->q.front());
    f->q.pop_front();
    f->bytes -= frame.data.size();
    return 1;
}

void r_frame_feeds::unsubscribe(uint64_t feed)
{
    lock_guard<mutex> g(_lock);
    auto it = _feeds.find(feed);
    if(it == _feeds.end())
        return;
    it->second->closed = true;
    it->second->q.clear();
    it->second->bytes = 0;
    it->second->cond.notify_all();
    _feeds.erase(it);
    _count = _feeds.size();
}

void r_frame_feeds::close_camera(const string& camera_id)
{
    lock_guard<mutex> g(_lock);
    for(auto& p : _feeds)
    {
        auto& f = *p.second;
        if(f.camera_id != camera_id || f.closed)
            continue;
        f.closed = true;
        f.q.clear();
        f.bytes = 0;
        f.cond.notify_all();
    }
}

void r_frame_feeds::close_all()
{
    lock_guard<mutex> g(_lock);
    for(auto& p : _feeds)
    {
        auto& f = *p.second;
        f.closed = true;
        f.q.clear();
        f.bytes = 0;
        f.cond.notify_all();
    }
}

void r_frame_feeds::post_video(const string& camera_id, const string& stream, const uint8_t* data, size_t size, bool key, int64_t ts, const string& codec_name, const string& codec_params)
{
    if(_count.load(memory_order_relaxed) == 0)
        return;
    lock_guard<mutex> g(_lock);
    ++_posts_seen;
    for(auto& p : _feeds)
    {
        auto& f = *p.second;
        if(f.closed || f.camera_id != camera_id || (f.media & VIDEO) == 0 || f.serving != stream)
            continue;
        _push(f, VIDEO, stream, data, size, key, ts, codec_name, codec_params);
    }
}

void r_frame_feeds::post_audio(const string& camera_id, const uint8_t* data, size_t size, int64_t ts, const string& codec_name, const string& codec_params)
{
    if(_count.load(memory_order_relaxed) == 0)
        return;
    lock_guard<mutex> g(_lock);
    ++_posts_seen;
    for(auto& p : _feeds)
    {
        auto& f = *p.second;
        if(f.closed || f.camera_id != camera_id || (f.media & AUDIO) == 0)
            continue;
        // A feed's audio is the main stream's, whichever stream its video is on.
        _push(f, AUDIO, "main", data, size, false, ts, codec_name, codec_params);
    }
}

void r_frame_feeds::_push(_feed& f, uint32_t media, const string& stream, const uint8_t* data, size_t size, bool key, int64_t ts, const string& codec_name, const string& codec_params)
{
    bool video = media == VIDEO;

    if(size > f.max_bytes || f.bytes + size > f.max_bytes)
    {
        // Doesn't fit: start again, video at its next key frame.
        f.q.clear();
        f.bytes = 0;
        f.need_key = true;
        f.video_gap = f.video_seen;
        f.audio_gap = f.audio_seen;
        if(size > f.max_bytes)
            return;
    }

    if(video && f.need_key && !key)
        return;

    r_feed_frame frame;
    frame.flags = media | (key ? KEY : 0);
    frame.ts = ts;
    frame.data.assign(data, data + size);
    frame.codec_name = codec_name;
    frame.codec_params = codec_params;
    frame.stream = stream;

    auto codec = codec_name + "\n" + codec_params;
    auto& seen = video ? f.video_seen : f.audio_seen;
    auto& last = video ? f.video_codec : f.audio_codec;
    auto& gap = video ? f.video_gap : f.audio_gap;
    if(!seen || codec != last)
        frame.flags |= CODEC_CHANGED;
    if(gap)
        frame.flags |= GAP;
    seen = true;
    last = codec;
    gap = false;
    if(video)
        f.need_key = false;

    f.bytes += size;
    f.q.push_back(std::move(frame));
    f.cond.notify_all();
}

size_t r_frame_feeds::feeds() const
{
    return _count.load();
}

size_t r_frame_feeds::posts_seen() const
{
    return _posts_seen.load();
}

size_t r_frame_feeds::queued_bytes(uint64_t feed) const
{
    lock_guard<mutex> g(_lock);
    auto it = _feeds.find(feed);
    return it == _feeds.end() ? 0 : it->second->bytes;
}
