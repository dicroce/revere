
#include "r_utils/r_args.h"

using namespace r_utils;
using namespace std;

vector<r_args::argument> r_utils::r_args::parse_arguments(int argc, char* argv[])
{
    vector<argument> out;
    for (int i = 1; i < argc; ++i) {
        string token = argv[i];
        if (token.size() >= 2 && token[0]=='-') {
            argument a;
            a.name = token;
            // if next exists and doesn’t start with '-', treat it as the value
            if (i+1 < argc && argv[i+1][0] != '-') {
                a.value = argv[++i];
            }
            out.push_back(move(a));
        }
        // else: we’re ignoring bare positional arguments here
    }
    return out;
}

string r_utils::r_args::get_required_argument(const vector<argument>& arguments, const string& name, const string& msg)
{
    auto arg = get_optional_argument(arguments, name);
    if(arg.is_null())
        R_THROW(((msg.length()!=0)?msg:r_string_utils::format("Required argument %s missing.", name.c_str())));
    return arg.value();
}

r_nullable<string> r_utils::r_args::get_optional_argument(const vector<argument>& arguments, const string& name, const std::string& def)
{
    r_nullable<string> result;
    string val;
    if(r_utils::r_args::check_argument(arguments, name, val))
        result.set_value(val);
    else if(def.length() != 0)
        result.set_value(def);

    return result;
}

vector<string> r_utils::r_args::get_all(const vector<argument>& arguments, const string& name)
{
    vector<string> result;
    vector<argument>::const_iterator i;
    for(i = arguments.begin(); i != arguments.end(); ++i)
    {
        if(i->name == name)
            result.push_back(i->value);
    }
    return result;
}

bool r_utils::r_args::check_argument(const vector<argument>& arguments, const string& name, string& value)
{
    vector<argument>::const_iterator i;
    for(i = arguments.begin(); i != arguments.end(); ++i)
    {
        if(i->name == name)
        {
            value = i->value;
            return true;
        }
    }

    return false;
}

bool r_utils::r_args::check_argument(const vector<r_args::argument>& arguments, const string& name)
{
    vector<argument>::const_iterator i;
    for(i = arguments.begin(); i != arguments.end(); ++i)
    {
        if(i->name == name)
            return true;
    }

    return false;
}
