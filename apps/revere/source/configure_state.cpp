
#include "configure_state.h"
#include "r_utils/r_file.h"
#include "r_utils/3rdparty/json/json.h"
#include "r_utils/r_logger.h"
#include <algorithm>

using namespace revere;
using namespace r_utils;
using namespace std;

using json = nlohmann::json;

configure_state::configure_state(const string& top_dir) :
    _top_dir(top_dir),
    _camera_answers(),
    _window_placement(),
    _sections_open(),
    _need_save(false),
    _mutex()
{
}

void configure_state::load()
{
    lock_guard<mutex> lock(_mutex);
    auto cfg_path = _top_dir + PATH_SLASH + "config" + PATH_SLASH + "revere_cfg.json";
    if(!r_fs::file_exists(cfg_path))
    {
        _need_save = true;
        return;
    }

    // Parse into locals and only take them once the whole file has read, so a
    // bad file can't leave half of it loaded.
    map<string, map<int, string>> camera_answers;
    optional<window_placement> placement;
    r_ui_utils::sections_open sections;
    try
    {
        auto cfg_buffer = r_fs::read_file(cfg_path);
        auto j = json::parse(string((char*)cfg_buffer.data(), cfg_buffer.size()));

        if(j.contains("camera_answers"))
        {
            for(auto& [camera_id, answers_j] : j["camera_answers"].items())
            {
                auto& answers = camera_answers[camera_id];
                for(auto& [num_str, answer] : answers_j.items())
                    answers[stoi(num_str)] = answer.get<string>();
            }
        }

        // Older files have no "window"; a malformed one is ignored (the window is centered).
        if(j.contains("window") && j["window"].is_object())
        {
            const auto& w = j["window"];
            auto is_int = [&](const char* k){ return w.contains(k) && w[k].is_number_integer(); };
            if(is_int("x") && is_int("y") && is_int("w") && is_int("h"))
            {
                window_placement wp;
                wp.rect = { w["x"].get<int>(), w["y"].get<int>(), w["w"].get<int>(), w["h"].get<int>() };
                wp.maximized = w.contains("maximized") && w["maximized"].is_boolean() && w["maximized"].get<bool>();
                placement = wp;
            }
        }

        // Older files have no "sections"; a bad entry leaves that section at its default.
        if(j.contains("sections"))
            sections = r_ui_utils::parse_sections_open(j["sections"]);
    }
    catch(const exception& e)
    {
        // A file cut short by a crash mid-save (before saves were atomic), or
        // edited by hand. Start with defaults rather than refuse to start, but
        // move the file aside first: the next save would otherwise replace it,
        // and the camera answers in it with nothing.
        string moved_to;
        try
        {
            moved_to = r_fs::quarantine_file(cfg_path);
        }
        catch(const exception& qe)
        {
            R_LOG_ERROR("CONFIG NOT LOADED: %s is unreadable (%s) and couldn't be moved aside (%s). Not saving over it this run.",
                        cfg_path.c_str(), e.what(), qe.what());
            _camera_answers.clear();
            _window_placement.reset();
            _sections_open.clear();
            _save_blocked = true;
            _load_problem = "Revere couldn't read its settings file, so it started with default settings and won't save any this run. The file is " + cfg_path + ".";
            return;
        }
        R_LOG_ERROR("CONFIG NOT LOADED: %s is unreadable (%s). Moved it to %s and started with defaults. "
                    "Camera answers, the window placement and list sections saved in it are not in use; restore them from that file.",
                    cfg_path.c_str(), e.what(), moved_to.c_str());
        _camera_answers.clear();
        _window_placement.reset();
        _sections_open.clear();
        _need_save = true;
        _load_problem = "Revere couldn't read its settings file, so it started with default settings. The old file was kept as " + moved_to + ".";
        return;
    }

    _camera_answers = std::move(camera_answers);
    _window_placement = placement;
    _sections_open = std::move(sections);
}

string configure_state::load_problem() const
{
    lock_guard<mutex> lock(_mutex);
    return _load_problem;
}

bool configure_state::need_save() const
{
    lock_guard<mutex> lock(_mutex);
    return _need_save;
}

void configure_state::save()
{
    lock_guard<mutex> lock(_mutex);
    if(_save_blocked)
    {
        _need_save = false;
        return;
    }

    auto ca_j = json::object();
    for(const auto& [camera_id, answers] : _camera_answers)
    {
        auto answers_j = json::object();
        for(const auto& [num, answer] : answers)
            answers_j[to_string(num)] = answer;
        ca_j[camera_id] = answers_j;
    }

    json j;
    j["camera_answers"] = ca_j;
    if(_window_placement)
    {
        const auto& wp = *_window_placement;
        j["window"] = { {"x", wp.rect.x}, {"y", wp.rect.y}, {"w", wp.rect.w}, {"h", wp.rect.h}, {"maximized", wp.maximized} };
    }
    if(!_sections_open.empty())
        j["sections"] = r_ui_utils::sections_open_to_json(_sections_open);

    auto cfg_txt = j.dump();
    auto cfg_dir = _top_dir + PATH_SLASH + "config";
    auto cfg_path = cfg_dir + PATH_SLASH + "revere_cfg.json";
    r_fs::mkdir_p(cfg_dir);
    // Written whole or not at all, so a crash mid-save can't leave a short file.
    r_fs::atomic_write_file((uint8_t*)cfg_txt.c_str(), cfg_txt.size(), cfg_path);

    _need_save = false;
}

optional<window_placement> configure_state::get_window_placement() const
{
    lock_guard<mutex> lock(_mutex);
    return _window_placement;
}

void configure_state::set_window_placement(const window_placement& placement)
{
    lock_guard<mutex> lock(_mutex);
    if(_window_placement && *_window_placement == placement)
        return;
    _window_placement = placement;
    _need_save = true;
}

bool configure_state::get_section_open(const string& key, bool def) const
{
    lock_guard<mutex> lock(_mutex);
    return r_ui_utils::section_open(_sections_open, key, def);
}

void configure_state::set_section_open(const string& key, bool open)
{
    lock_guard<mutex> lock(_mutex);
    auto found = _sections_open.find(key);
    if(found != _sections_open.end() && found->second == open)
        return;
    _sections_open[key] = open;
    _need_save = true;
}

bool configure_state::has_camera_answers(const string& camera_id) const
{
    lock_guard<mutex> lock(_mutex);
    return _camera_answers.find(camera_id) != _camera_answers.end();
}

optional<string> configure_state::get_camera_answer(const string& camera_id, int question_num) const
{
    lock_guard<mutex> lock(_mutex);
    auto camera_it = _camera_answers.find(camera_id);
    if(camera_it == _camera_answers.end())
        return nullopt;
    auto answer_it = camera_it->second.find(question_num);
    if(answer_it == camera_it->second.end())
        return nullopt;
    string result = answer_it->second;
    transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

void configure_state::set_camera_answer(const string& camera_id, int question_num, const string& answer)
{
    lock_guard<mutex> lock(_mutex);
    _camera_answers[camera_id][question_num] = answer;
    _need_save = true;
}

void configure_state::set_camera_answers(const string& camera_id, const map<int, string>& answers)
{
    lock_guard<mutex> lock(_mutex);
    _camera_answers[camera_id] = answers;
    _need_save = true;
}
