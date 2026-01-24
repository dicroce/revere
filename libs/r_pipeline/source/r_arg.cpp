
#include "r_pipeline/r_arg.h"

using namespace std;
using namespace r_utils;
using namespace r_pipeline;

r_arg::r_arg() :
    _name(),
    _value()
{
}

r_arg::r_arg(const string& name, const string& value) :
    _name(name),
    _value(value)
{
}

r_arg::r_arg(const r_arg& obj) :
    _name(obj._name),
    _value(obj._value)
{
}

r_arg::~r_arg() noexcept
{
}

r_arg& r_arg::operator=(const r_arg& obj)
{
    _name = obj._name;
    _value = obj._value;
    return *this;
}

void r_arg::set_name(const string& name)
{
    _name = name;
}

string r_arg::get_name() const
{
    return _name;
}

bool r_arg::has_value() const
{
    return _value;
}

void r_arg::set_value(const string& value)
{
    _value.set_value(value);
}

r_nullable<string> r_arg::get_value() const
{
    return _value;
}

void r_pipeline::add_argument(vector<r_arg>& arguments, const string& name, const string& value)
{
    r_arg nv(name, value);
    arguments.push_back(nv);
}
