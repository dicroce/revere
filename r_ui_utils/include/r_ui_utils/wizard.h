#ifndef __r_ui_utils_wizard_h
#define __r_ui_utils_wizard_h

#include "imgui/imgui.h"

#include <string>
#include <mutex>
#include <map>
#include <functional>

namespace r_ui_utils
{

class wizard final
{
public:
    void next(const std::string& name)
    {
        std::lock_guard g(_lock);
        _active = true;
        ImGui::CloseCurrentPopup();
        _current_name = name;
    }
    void cancel()
    {
        std::lock_guard g(_lock);
        _active = false;
        ImGui::CloseCurrentPopup();
    }

    void add_step(const std::string& name, std::function<void()> cb)
    {
        _steps.insert(std::make_pair(name, cb));
    }

    void operator()()
    {
        std::lock_guard g(_lock);

        if(!_active)
            return;

        auto it = _steps.find(_current_name);
        if(it == _steps.end())
            R_THROW(("Unknow wizard step name!"));

        it->second();
    }

    bool active() const
    {
        std::lock_guard g(_lock);
        return _active;
    }

private:
    std::map<std::string, std::function<void()>> _steps;
    std::string _current_name;
    bool _active {false};
    mutable std::recursive_mutex _lock;
};

}

#endif
