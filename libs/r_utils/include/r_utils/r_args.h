
#ifndef r_utils_r_args_h
#define r_utils_r_args_h

#include "r_utils/r_macro.h"
#include "r_utils/r_nullable.h"
#include <string>
#include <vector>

namespace r_utils
{

namespace r_args
{

struct argument
{
    std::string name;
    std::string value;
};

R_API std::vector<argument> parse_arguments(int argc, char* argv[]);

R_API std::string get_required_argument(const std::vector<argument>& arguments, const std::string& opt, const std::string& msg = "");
R_API r_nullable<std::string> get_optional_argument(const std::vector<argument>& arguments, const std::string& opt, const std::string& def = "");
R_API std::vector<std::string> get_all(const std::vector<argument>& arguments, const std::string& opt);

R_API bool check_argument(const std::vector<argument>& arguments, const std::string& opt, std::string& arg);
R_API bool check_argument(const std::vector<argument>& arguments, const std::string& opt);

}

}

#endif
