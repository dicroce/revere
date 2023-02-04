
#include "r_utils/r_args.h"
#include "r_utils/r_string_utils.h"

using namespace r_utils;
using namespace std;

vector<r_args::argument> r_utils::r_args::parse_arguments(int argc, char* argv[])
{
    vector<argument> arguments;

    struct argument currentArgument;

    int argumentIndex = -1;

    for(int i = 0; i < argc; ++i)
    {
        string token = argv[i];

        if(r_string_utils::starts_with(token, "-"))
        {
            if(argumentIndex != -1)
            {
                currentArgument.opt = argv[argumentIndex];
                arguments.push_back(currentArgument);
                currentArgument.arg = "";
            }

            currentArgument.opt = token;
            argumentIndex = i;
        }
        else
        {
            if(argumentIndex != -1)
            {
                if(currentArgument.arg.length() > 0)
                    currentArgument.arg += string(" ") + token;
                else currentArgument.arg = token;
            }
        }

        if((i + 1) == argc)
        {
            if(argumentIndex != -1)
                arguments.push_back(currentArgument);
        }
    }

    return arguments;
}

string r_utils::r_args::get_required_argument(const vector<argument>& arguments, const string& opt, const string& msg)
{
    auto arg = get_optional_argument(arguments, opt);
    if(arg.is_null())
        R_THROW(((msg.length()!=0)?msg:r_string_utils::format("Required argument %s missing.", opt.c_str())));
    return arg.value();
}

r_nullable<string> r_utils::r_args::get_optional_argument(const vector<argument>& arguments, const string& opt, const std::string& def)
{
    r_nullable<string> result;
    string val;
    if(r_utils::r_args::check_argument(arguments, opt, val))
        result.set_value(val);
    else if(def.length() != 0)
        result.set_value(def);

    return result;
}

vector<string> r_utils::r_args::get_all(const vector<argument>& arguments, const string& opt)
{
    vector<string> result;
    vector<argument>::const_iterator i;
    for(i = arguments.begin(); i != arguments.end(); ++i)
    {
        if(i->opt == opt)
            result.push_back(i->arg);
    }
    return result;
}

bool r_utils::r_args::check_argument(const vector<argument>& arguments, const string& opt, string& arg)
{
    vector<argument>::const_iterator i;
    for(i = arguments.begin(); i != arguments.end(); ++i)
    {
        if(i->opt == opt)
        {
            arg = i->arg;
            return true;
        }
    }

    return false;
}

bool r_utils::r_args::check_argument(const vector<r_args::argument>& arguments, const string& opt)
{
    vector<argument>::const_iterator i;
    for(i = arguments.begin(); i != arguments.end(); ++i)
    {
        if(i->opt == opt)
            return true;
    }

    return false;
}
