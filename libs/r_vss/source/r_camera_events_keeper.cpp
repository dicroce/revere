#include "r_vss/r_camera_events_keeper.h"

using namespace r_vss;
using namespace r_cameras;
using namespace std;

static string _nullable(const r_utils::r_nullable<string>& v) { return v.is_null() ? string("\x01") : v.value(); }

string r_camera_events_keeper::settings_key(const r_disco::r_camera& camera)
{
    // Separated by a byte no setting holds, so two different settings never make the same key.
    const char sep = '\x1f';
    string key;
    key += _nullable(camera.ipv4) + sep;
    key += _nullable(camera.xaddrs) + sep;
    key += (camera.onvif_port.is_null() ? string("\x01") : to_string(camera.onvif_port.value())) + sep;
    key += _nullable(camera.onvif_protocol) + sep;
    key += _nullable(camera.rtsp_username) + sep;
    key += _nullable(camera.rtsp_password);
    return key;
}

r_camera_events_keeper::r_camera_events_keeper(factory_fn factory, always_fn always) :
    _factory(std::move(factory)),
    _always(std::move(always))
{
}

r_camera_events_keeper::~r_camera_events_keeper() noexcept
{
    clear();
}

void r_camera_events_keeper::_deliver(const weak_ptr<slot>& w, const r_camera_transition& t)
{
    auto s = w.lock();
    if (!s)
        return;
    lock_guard<mutex> d(s->deliver);
    sink_fn sink;
    {
        lock_guard<mutex> g(s->lock);
        auto k = make_pair(t.what, t.source);
        if (t.active)
            s->open.insert(k);
        else
            s->open.erase(k);
        sink = s->sink;
    }
    if (sink)
        sink(t);
}

void r_camera_events_keeper::_detach(slot& s)
{
    // The caller holds s.deliver.
    sink_fn sink;
    set<pair<string, string>> open;
    {
        lock_guard<mutex> g(s.lock);
        sink = std::move(s.sink);
        s.sink = nullptr;
        s.owner = nullptr;
        open = s.open;
    }
    if (!sink)
        return;
    for (auto& o : open)
    {
        r_camera_transition t;
        t.what = o.first;
        t.source = o.second;
        t.active = false;
        sink(t);
    }
}

vector<r_camera_events_keeper::events_ptr> r_camera_events_keeper::ensure(const r_disco::r_camera& camera)
{
    vector<events_ptr> replaced;
    auto key = settings_key(camera);
    shared_ptr<slot> old;
    {
        lock_guard<mutex> g(_lock);
        auto it = _slots.find(camera.id);
        if (it != _slots.end())
        {
            if (it->second->key == key)
                return replaced;
            old = it->second;
            _slots.erase(it);
        }
        auto s = make_shared<slot>();
        s->camera_id = camera.id;
        s->key = key;
        weak_ptr<slot> w = s;
        auto always = _always;
        auto id = camera.id;
        s->events = _factory(camera, [w, always, id](const r_camera_transition& t) {
            _deliver(w, t);
            if (always)
                always(id, t);
        });
        // None for these settings (the camera has no events to watch now): no slot, but the one replaced still ends.
        if (s->events)
        {
            s->events->start();
            _slots[camera.id] = s;
        }
    }
    if (old)
    {
        {
            lock_guard<mutex> d(old->deliver);
            _detach(*old);
        }
        events_ptr e;
        {
            lock_guard<mutex> g(old->lock);
            e = std::move(old->events);
        }
        if (e)
        {
            e->request_stop();
            replaced.push_back(std::move(e));
        }
    }
    return replaced;
}

shared_ptr<r_camera_events_keeper::slot> r_camera_events_keeper::_find(const string& camera_id) const
{
    lock_guard<mutex> g(_lock);
    auto it = _slots.find(camera_id);
    return it == _slots.end() ? nullptr : it->second;
}

void r_camera_events_keeper::attach(const string& camera_id, const void* owner, sink_fn sink)
{
    auto s = _find(camera_id);
    if (!s)
        return;
    lock_guard<mutex> d(s->deliver);
    _detach(*s);
    set<pair<string, string>> open;
    {
        lock_guard<mutex> g(s->lock);
        s->owner = owner;
        s->sink = sink;
        open = s->open;
    }
    for (auto& o : open)
    {
        r_camera_transition t;
        t.what = o.first;
        t.source = o.second;
        t.active = true;
        sink(t);
    }
}

void r_camera_events_keeper::detach(const string& camera_id, const void* owner)
{
    auto s = _find(camera_id);
    if (!s)
        return;
    lock_guard<mutex> d(s->deliver);
    {
        lock_guard<mutex> g(s->lock);
        if (s->owner != owner)
            return;
    }
    _detach(*s);
}

r_camera_events_keeper::events_ptr r_camera_events_keeper::drop(const string& camera_id)
{
    shared_ptr<slot> s;
    {
        lock_guard<mutex> g(_lock);
        auto it = _slots.find(camera_id);
        if (it == _slots.end())
            return nullptr;
        s = it->second;
        _slots.erase(it);
    }
    {
        lock_guard<mutex> d(s->deliver);
        _detach(*s);
    }
    events_ptr e;
    {
        lock_guard<mutex> g(s->lock);
        e = std::move(s->events);
    }
    e->request_stop();
    return e;
}

vector<r_camera_events_keeper::events_ptr> r_camera_events_keeper::drop_except(const set<string>& keep)
{
    vector<string> gone;
    {
        lock_guard<mutex> g(_lock);
        for (auto& kv : _slots)
            if (keep.count(kv.first) == 0)
                gone.push_back(kv.first);
    }
    vector<events_ptr> dropped;
    for (auto& id : gone)
        if (auto e = drop(id))
            dropped.push_back(std::move(e));
    return dropped;
}

void r_camera_events_keeper::request_stop_all()
{
    lock_guard<mutex> g(_lock);
    for (auto& kv : _slots)
    {
        lock_guard<mutex> s(kv.second->lock);
        if (kv.second->events)
            kv.second->events->request_stop();
    }
}

void r_camera_events_keeper::clear()
{
    set<string> none;
    auto all = drop_except(none);
    all.clear();   // each waits for its thread here, outside every lock
}

bool r_camera_events_keeper::has(const string& camera_id) const
{
    return (bool)_find(camera_id);
}

r_camera_events_status r_camera_events_keeper::status(const string& camera_id) const
{
    auto s = _find(camera_id);
    if (!s)
        return r_camera_events_status();
    lock_guard<mutex> g(s->lock);
    return s->events ? s->events->status() : r_camera_events_status();
}

void r_camera_events_keeper::read_clock(const string& camera_id)
{
    auto s = _find(camera_id);
    if (!s)
        return;
    lock_guard<mutex> g(s->lock);
    if (s->events)
        s->events->read_clock();
}

vector<r_camera_span> r_camera_events_keeper::spans(const string& camera_id, int64_t since_ms) const
{
    auto s = _find(camera_id);
    if (!s)
        return {};
    lock_guard<mutex> g(s->lock);
    return s->events ? s->events->spans(since_ms) : vector<r_camera_span>();
}
