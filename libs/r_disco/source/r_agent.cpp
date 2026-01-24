
#include "r_disco/r_agent.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_md5.h"
#include <algorithm>
#include <iterator>
#include <vector>
#include <functional>

using namespace r_disco;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

r_agent::r_agent(const std::string& top_dir) :
    _th(),
    _running(false),
    _onvif_provider(make_unique<r_onvif_provider>(top_dir, this)),
    _changed_streams_cb(),
    _top_dir(top_dir),
    _timer(),
    _device_config_hashes_mutex(),
    _device_config_hashes(),
    _credential_cb()
{
}

r_agent::~r_agent() noexcept
{
    stop();
}

void r_agent::start()
{
    _running = true;
    _th = std::thread(&r_agent::_entry_point, this);
}

void r_agent::stop()
{
    if(_running)
    {
        _running = false;
        _th.join();
    }
}

void r_agent::interrogate_camera(
    const std::string& camera_name,
    const std::string& ipv4,
    const std::string& xaddrs,
    const std::string& address,
    r_nullable<string> username,
    r_nullable<string> password
)
{
    r_md5 hash;
    hash.update((uint8_t*)address.c_str(), address.size());
    hash.finalize();
    auto id = hash.get_as_uuid();

    auto sc = _onvif_provider->interrogate_camera(id, camera_name, ipv4, xaddrs, address, username, password);

    vector<pair<r_stream_config, string>> output;
    output.push_back(make_pair(sc.value(), hash_stream_config(sc.value())));

    if(_changed_streams_cb)
        _changed_streams_cb(output);
}

void r_agent::forget(const std::string& id)
{
    lock_guard<mutex> lock(_device_config_hashes_mutex);
    _device_config_hashes.erase(id);
}

pair<r_nullable<string>, r_nullable<string>> r_agent::_get_credentials(const std::string& id)
{
    if(!_credential_cb)
        R_THROW(("Please set a credential callback on r_agent before calling start."));

    return _credential_cb(id);
}

bool r_agent::_is_recording(const std::string& id)
{
    if(!_is_recording_cb)
        R_THROW(("Please set a is_recording callback on r_agent before calling start."));
    
    return _is_recording_cb(id);
}

void r_agent::_entry_point()
{
    auto default_max_sleep = milliseconds(100);

    while(_running)
    {
        if(_timer.get_num_timed_events() == 0)
        {
            _timer.add_timed_event(
                steady_clock::now(),
                seconds(60),
                std::bind(&r_agent::_process_new_or_changed_streams_configs, this),
                true
            );
        }
        auto delta_millis = _timer.update(default_max_sleep, steady_clock::now());
        this_thread::sleep_for(delta_millis);
    }
}

void r_agent::_process_new_or_changed_streams_configs()
{
    lock_guard<mutex> lock(_device_config_hashes_mutex);
    // fetch all our stream_configs from all providers
    auto devices = _onvif_provider->poll();

    if(!devices.empty())
    {
        // populate new_or_changed with all devices that are new or changed
        vector<r_stream_config> new_or_changed;
        copy_if(
            begin(devices), end(devices), 
            back_inserter(new_or_changed),
            [this](const r_stream_config& sc){
                auto new_hash = hash_stream_config(sc);
                auto found = this->_device_config_hashes.find(sc.id);
                if(found == this->_device_config_hashes.end() || new_hash != found->second)
                {
                    this->_device_config_hashes[sc.id] = new_hash;
                    return true;
                }
                return false;
            }
        );

        vector<pair<r_stream_config, string>> output;
        transform(
            begin(new_or_changed), end(new_or_changed),
            back_inserter(output),
            [](const r_stream_config& sc){
                return make_pair(sc, hash_stream_config(sc));
            }
        );

        if(_changed_streams_cb)
            _changed_streams_cb(output);
    }
}
