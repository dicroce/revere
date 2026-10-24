#include "r_vss/r_keep_awake.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"
#include "r_utils/3rdparty/json/json.h"
#include <chrono>

using namespace r_vss;
using namespace r_utils;
using namespace std;
using json = nlohmann::json;

static int64_t _wall_ms()
{
    return chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
}

static int64_t _steady_ms()
{
    return chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
}

string r_vss::power_source_name(r_power_source source)
{
    switch(source)
    {
        case r_power_source::ac: return "ac";
        case r_power_source::battery: return "battery";
        default: return "unknown";
    }
}

bool r_vss::keep_awake_wanted(const r_keep_awake_settings& settings, r_power_source source)
{
    if(!settings.enabled)
        return false;
    return source != r_power_source::battery || settings.on_battery;
}

r_keep_awake_settings r_vss::parse_keep_awake_settings(const string& json_text)
{
    r_keep_awake_settings s;
    auto j = json::parse(json_text, nullptr, false);
    if(!j.is_object())
        return s;
    if(j.contains("enabled") && j["enabled"].is_boolean())
        s.enabled = j["enabled"].get<bool>();
    if(j.contains("on_battery") && j["on_battery"].is_boolean())
        s.on_battery = j["on_battery"].get<bool>();
    return s;
}

string r_vss::keep_awake_settings_json(const r_keep_awake_settings& settings)
{
    return json{{"enabled", settings.enabled}, {"on_battery", settings.on_battery}}.dump();
}

r_power_source r_vss::power_source_from_supplies(const vector<r_power_supply>& supplies)
{
    bool has_battery = false;
    for(const auto& s : supplies)
    {
        // A wireless mouse's battery says nothing about the computer's power.
        if(s.scope == "Device")
            continue;
        if(s.type == "Battery")
            has_battery = true;
        else if(s.online)
            return r_power_source::ac;
    }
    return has_battery ? r_power_source::battery : r_power_source::ac;
}

r_power_source r_vss::power_source_from_status(uint8_t ac_line_status, uint8_t battery_flag)
{
    if(ac_line_status == 1)
        return r_power_source::ac;
    if(battery_flag != 255 && (battery_flag & 128))
        return r_power_source::ac;
    if(ac_line_status == 0)
        return r_power_source::battery;
    return r_power_source::unknown;
}

void r_sleep_gap_detector::on_sleep(int64_t wall_ms)
{
    _sleep_ms = wall_ms;
}

void r_sleep_gap_detector::skip(int64_t busy_ms)
{
    if(_last_tick_ms >= 0 && busy_ms > 0)
        _last_tick_ms += busy_ms;
}

optional<r_power_event> r_sleep_gap_detector::tick(int64_t wall_ms)
{
    auto last = _last_tick_ms;
    _last_tick_ms = wall_ms;
    if(last < 0)
        return nullopt;

    auto delta = wall_ms - last;
    // A sleep the OS called off is left alone: by the next gap it's too old
    // to count (below).
    if(delta < GAP_MS)
        return nullopt;

    r_power_event e;
    e.end_ms = wall_ms;
    if(_sleep_ms >= 0 && _sleep_ms >= last - GAP_MS && _sleep_ms <= wall_ms)
    {
        e.what = "asleep";
        e.start_ms = _sleep_ms;
    }
    else
    {
        e.what = "paused";
        e.start_ms = last;
    }
    _sleep_ms = -1;
    return e;
}

r_keep_awake::r_keep_awake(const string& top_dir) :
    _config_dir(top_dir + PATH_SLASH + "config"),
    _settings_path(_config_dir + PATH_SLASH + "keep_awake.json"),
    _events_path(_config_dir + PATH_SLASH + "power_events.json")
{
    try
    {
        if(r_fs::file_exists(_settings_path))
        {
            auto buf = r_fs::read_file(_settings_path);
            _settings = parse_keep_awake_settings(string((char*)buf.data(), buf.size()));
        }
    }
    catch(const exception& ex)
    {
        R_LOG_WARNING("Keep awake: couldn't read %s, using the defaults: %s", _settings_path.c_str(), ex.what());
    }

    try
    {
        if(r_fs::file_exists(_events_path))
        {
            auto buf = r_fs::read_file(_events_path);
            auto j = json::parse(string((char*)buf.data(), buf.size()), nullptr, false);
            if(j.is_array())
            {
                for(const auto& ej : j)
                {
                    if(!ej.is_object() || !ej.contains("what") || !ej["what"].is_string() ||
                       !ej.contains("start_ms") || !ej["start_ms"].is_number_integer() ||
                       !ej.contains("end_ms") || !ej["end_ms"].is_number_integer())
                        continue;
                    _events.push_back({ej["what"].get<string>(), ej["start_ms"].get<int64_t>(), ej["end_ms"].get<int64_t>()});
                    if(_events.size() >= (size_t)MAX_EVENTS)
                        break;
                }
            }
        }
    }
    catch(const exception& ex)
    {
        R_LOG_WARNING("Keep awake: couldn't read %s: %s", _events_path.c_str(), ex.what());
    }
}

r_keep_awake::~r_keep_awake() noexcept
{
    stop();
}

void r_keep_awake::start()
{
    {
        lock_guard<mutex> g(_mutex);
        if(_running)
            return;
        _running = true;
    }

    R_LOG_INFO("Keep awake: %s%s.",
               _settings.enabled ? "on" : "off",
               _settings.enabled ? (_settings.on_battery ? ", also on battery" : ", while plugged in") : "");

    try
    {
        _watcher = make_unique<r_sleep_watcher>([this](bool sleeping) { _on_os_sleep(sleeping); });
    }
    catch(const exception& ex)
    {
        R_LOG_WARNING("Keep awake: sleep notifications unavailable: %s", ex.what());
    }

    _th = thread(&r_keep_awake::_entry_point, this);
}

void r_keep_awake::stop()
{
    {
        lock_guard<mutex> g(_mutex);
        if(!_running)
            return;
        _running = false;
    }
    _cond.notify_all();
    if(_th.joinable())
        _th.join();
    _watcher.reset();

    // The ticker has stopped: nothing else touches the blocker now.
    if(_blocker.held())
    {
        _blocker.release();
        R_LOG_INFO("Keep awake: released.");
    }
    lock_guard<mutex> g(_mutex);
    _held = false;
}

r_keep_awake_settings r_keep_awake::get_settings() const
{
    lock_guard<mutex> g(_mutex);
    return _settings;
}

void r_keep_awake::set_settings(const r_keep_awake_settings& settings)
{
    {
        lock_guard<mutex> g(_mutex);
        if(settings == _settings)
            return;
        _settings = settings;
        // Right away for the status; the ticker holds or releases next.
        _wanted = keep_awake_wanted(settings, _power);
        _changed = true;

        auto txt = keep_awake_settings_json(settings);
        try
        {
            r_fs::mkdir_p(_config_dir);
            r_fs::atomic_write_file((const uint8_t*)txt.c_str(), txt.size(), _settings_path);
        }
        catch(const exception& ex)
        {
            R_LOG_ERROR("Keep awake: couldn't save %s: %s", _settings_path.c_str(), ex.what());
        }

        R_LOG_INFO("Keep awake: set %s%s.",
                   settings.enabled ? "on" : "off",
                   settings.enabled ? (settings.on_battery ? ", also on battery" : ", while plugged in") : "");
    }
    _cond.notify_all();
}

r_keep_awake_status r_keep_awake::get_status() const
{
    lock_guard<mutex> g(_mutex);
    r_keep_awake_status s;
    s.settings = _settings;
    s.power = _power;
    s.wanted = _wanted;
    s.held = _held;
    s.problem = _problem;
    s.events.assign(_events.begin(), _events.end());
    return s;
}

void r_keep_awake::_entry_point()
{
    _apply(true);

    unique_lock<mutex> g(_mutex);
    while(_running)
    {
        _cond.wait_for(g, chrono::milliseconds(TICK_MS), [this]() { return !_running || _changed; });
        if(!_running)
            break;
        bool changed = _changed;
        _changed = false;

        auto wall = _wall_ms();
        auto e = _gaps.tick(wall);
        g.unlock();

        if(e)
        {
            auto secs = (e->end_ms - e->start_ms) / 1000;
            if(e->what == "asleep")
                R_LOG_WARNING("Keep awake: the computer was asleep for %lld s.", (long long)secs);
            else
                R_LOG_WARNING("Keep awake: Revere didn't run for %lld s (the computer may have slept without saying so, or Revere stalled).", (long long)secs);
            _add_event(*e);
        }

        auto busy_from = _steady_ms();
        _apply(changed);
        auto busy_ms = _steady_ms() - busy_from;
        g.lock();
        _gaps.skip(busy_ms);
    }
}

// Holds or releases the request to match the setting and the power source.
// Called on the ticker thread only. The OS calls are made without the lock:
// on Linux they're D-Bus round trips that can take seconds, and the desktop
// reads the status every frame.
void r_keep_awake::_apply(bool force_retry)
{
    auto power = current_power_source();

    bool wanted = false;
    bool enabled = false;
    {
        lock_guard<mutex> g(_mutex);
        if(power != _power)
        {
            if(_power != r_power_source::unknown || power != r_power_source::unknown)
                R_LOG_INFO("Keep awake: power source is %s.", power_source_name(power).c_str());
            _power = power;
        }
        _wanted = keep_awake_wanted(_settings, power);
        wanted = _wanted;
        enabled = _settings.enabled;

        if(!wanted)
        {
            _problem.clear();
            if(!_held)
                return;
        }
        else
        {
            if(_held)
                return;
            auto now = _steady_ms();
            if(!force_retry && !_problem.empty() && now - _last_try_ms < RETRY_MS)
                return;
            _last_try_ms = now;
        }
    }

    if(!wanted)
    {
        _blocker.release();
        lock_guard<mutex> g(_mutex);
        _held = false;
        R_LOG_INFO("Keep awake: released (%s).", enabled ? "on battery" : "turned off");
        return;
    }

    auto problem = _blocker.hold(KEEP_AWAKE_REASON);

    lock_guard<mutex> g(_mutex);
    _held = _blocker.held();
    if(problem.empty())
    {
        R_LOG_INFO("Keep awake: holding \"%s\".", KEEP_AWAKE_REASON);
        _problem.clear();
    }
    else
    {
        if(problem != _problem)
            R_LOG_WARNING("Keep awake: the computer may still sleep: %s", problem.c_str());
        _problem = problem;
    }
}

void r_keep_awake::_on_os_sleep(bool sleeping)
{
    if(sleeping)
    {
        R_LOG_INFO("Keep awake: the computer is going to sleep.");
        lock_guard<mutex> g(_mutex);
        _gaps.on_sleep(_wall_ms());
    }
    else
    {
        R_LOG_INFO("Keep awake: the computer woke up.");
        // Tick now, so the event is in before anyone asks.
        {
            lock_guard<mutex> g(_mutex);
            _changed = true;
        }
        _cond.notify_all();
    }
}

void r_keep_awake::_add_event(const r_power_event& e)
{
    lock_guard<mutex> g(_mutex);
    _events.push_front(e);
    while(_events.size() > (size_t)MAX_EVENTS)
        _events.pop_back();
    _save_events();
}

void r_keep_awake::_save_events() const
{
    auto j = json::array();
    for(const auto& e : _events)
        j.push_back({{"what", e.what}, {"start_ms", e.start_ms}, {"end_ms", e.end_ms}});
    auto txt = j.dump();
    try
    {
        r_fs::mkdir_p(_config_dir);
        r_fs::atomic_write_file((const uint8_t*)txt.c_str(), txt.size(), _events_path);
    }
    catch(const exception& ex)
    {
        R_LOG_ERROR("Keep awake: couldn't save %s: %s", _events_path.c_str(), ex.what());
    }
}
