#include "r_vss/r_export_output.h"
#include "r_utils/r_file.h"
#include "r_utils/r_exception.h"

using namespace r_vss;
using namespace r_utils;
using namespace std;

r_export_output::r_export_output(const string& exports_path, const string& file_name) :
    _output_path(exports_path + PATH_SLASH + file_name)
{
    auto dot = file_name.rfind('.');
    auto ext = (dot != string::npos) ? file_name.substr(dot) : string{};
    _temp_path = r_fs::temp_file_name(exports_path, "tmp_") + ext;
}

r_export_output::~r_export_output() noexcept
{
    try
    {
        if(r_fs::file_exists(_temp_path))
            r_fs::remove_file(_temp_path);
    }
    catch(...) {}
}

void r_export_output::skipped(const string& why)
{
    if(_first_skip.empty())
        _first_skip = why;
}

void r_export_output::finish(r_av::r_muxer& muxer)
{
    if(!_opened)
        R_THROW(("Export wrote nothing: %s", _first_skip.empty() ? "no chunk was written" : _first_skip.c_str()));

    muxer.finalize();
    r_fs::atomic_rename_file(_temp_path, _output_path);
}
