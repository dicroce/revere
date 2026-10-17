#include "r_vss/r_snapshot_cache.h"

using namespace r_vss;
using namespace std;
using namespace std::chrono;

r_snapshot_cache::r_snapshot_cache(steady_clock::duration ttl, size_t max_entries) :
    _ttl(ttl),
    _max_entries(max_entries)
{
}

vector<uint8_t> r_snapshot_cache::get(const string& key, const function<vector<uint8_t>()>& make)
{
    promise<vector<uint8_t>> p;
    shared_future<vector<uint8_t>> existing;
    {
        lock_guard<mutex> g(_lock);
        _expire(steady_clock::now());

        auto found = _entries.find(key);
        if(found != _entries.end())
            existing = found->second.result;
        else _entries[key].result = p.get_future().share();
    }

    // Built and fresh, or being built by another caller: wait for it. A failed
    // build rethrows here.
    if(existing.valid())
        return existing.get();

    try
    {
        auto v = make();
        {
            lock_guard<mutex> g(_lock);
            auto found = _entries.find(key);
            if(found != _entries.end())
            {
                found->second.built = true;
                found->second.built_at = steady_clock::now();
            }
        }
        p.set_value(v);
        return v;
    }
    catch(...)
    {
        {
            lock_guard<mutex> g(_lock);
            _entries.erase(key);
        }
        p.set_exception(current_exception());
        throw;
    }
}

size_t r_snapshot_cache::size() const
{
    lock_guard<mutex> g(_lock);
    return _entries.size();
}

void r_snapshot_cache::_expire(steady_clock::time_point now)
{
    for(auto i = _entries.begin(); i != _entries.end();)
    {
        if(i->second.built && now - i->second.built_at >= _ttl)
            i = _entries.erase(i);
        else ++i;
    }

    // Still too many: drop the oldest built ones (never one being built).
    while(_entries.size() > _max_entries)
    {
        auto oldest = _entries.end();
        for(auto i = _entries.begin(); i != _entries.end(); ++i)
            if(i->second.built && (oldest == _entries.end() || i->second.built_at < oldest->second.built_at))
                oldest = i;
        if(oldest == _entries.end())
            break;
        _entries.erase(oldest);
    }
}
