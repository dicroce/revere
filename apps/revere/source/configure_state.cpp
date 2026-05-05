
#include "configure_state.h"
#include "r_utils/r_file.h"
#include "r_utils/3rdparty/json/json.h"
#include <algorithm>

using namespace revere;
using namespace r_utils;
using namespace std;

using json = nlohmann::json;

configure_state::configure_state(const string& top_dir) :
    _top_dir(top_dir),
    _camera_answers(),
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

    auto cfg_buffer = r_fs::read_file(cfg_path);
    auto j = json::parse(string((char*)cfg_buffer.data(), cfg_buffer.size()));

    _camera_answers.clear();
    if(j.contains("camera_answers"))
    {
        for(auto& [camera_id, answers_j] : j["camera_answers"].items())
        {
            auto& answers = _camera_answers[camera_id];
            for(auto& [num_str, answer] : answers_j.items())
                answers[stoi(num_str)] = answer.get<string>();
        }
    }
}

bool configure_state::need_save() const
{
    lock_guard<mutex> lock(_mutex);
    return _need_save;
}

void configure_state::save()
{
    lock_guard<mutex> lock(_mutex);

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

    auto cfg_txt = j.dump();
    auto cfg_dir = _top_dir + PATH_SLASH + "config";
    auto cfg_path = cfg_dir + PATH_SLASH + "revere_cfg.json";
    r_fs::mkdir_p(cfg_dir);
    r_fs::write_file((uint8_t*)cfg_txt.c_str(), cfg_txt.size(), cfg_path);

    _need_save = false;
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
