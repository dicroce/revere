#ifndef r_pipeline_r_arg_h
#define r_pipeline_r_arg_h

#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include <string>
#include <vector>

namespace r_pipeline
{

class r_arg final
{
public:
    R_API r_arg();
    R_API r_arg(const std::string& name, const std::string& value);
    R_API r_arg(const r_arg& obj);

    R_API ~r_arg() noexcept;

    R_API r_arg& operator=(const r_arg& obj);

    R_API void set_name(const std::string& name);
    R_API std::string get_name() const;

    R_API bool has_value() const;
    R_API void set_value(const std::string& value);
    R_API r_utils::r_nullable<std::string> get_value() const;

private:
    std::string _name;
    r_utils::r_nullable<std::string> _value;
};

R_API void add_argument(std::vector<r_arg>& arguments, const std::string& name, const std::string& value);

}

#endif
