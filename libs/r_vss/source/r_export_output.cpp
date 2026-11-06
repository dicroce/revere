#include "r_vss/r_export_output.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"

using namespace r_utils;
using namespace r_vss;
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
    if(_committed)
        return;
    try
    {
        if(r_fs::file_exists(_temp_path))
            r_fs::remove_file(_temp_path);
    }
    catch(...) {}
}

string r_vss::export_message(const exception& ex)
{
    string msg = ex.what();
    auto eol = msg.find('\n');
    return eol == string::npos ? msg : msg.substr(0, eol);
}

bool r_export_output::take(int stream_id, int64_t ts)
{
    auto found = _last_ts.find(stream_id);
    if(found != _last_ts.end() && ts <= found->second)
        return false;
    _last_ts[stream_id] = ts;
    return true;
}

void r_export_output::skipped(const string& reason)
{
    ++_skipped;
    _last_skip = reason;
}

void r_export_output::check(bool written, bool stopped, const string& range) const
{
    if(stopped)
        R_THROW(("Export of %s stopped: revere is shutting down.", range.c_str()));
    if(!written)
    {
        if(_skipped > 0)
            R_THROW(("Nothing to export in %s: no recording there, or none of it could be read (%zu chunk(s) skipped, the last: %s)",
                     range.c_str(), _skipped, _last_skip.c_str()));
        R_THROW(("Nothing to export in %s.", range.c_str()));
    }
}

void r_export_output::commit()
{
    r_fs::atomic_rename_file(_temp_path, _output_path);
    _committed = true;
    if(_skipped > 0)
        R_LOG_WARNING("Export %s written with %zu chunk(s) skipped, the last: %s", _output_path.c_str(), _skipped, _last_skip.c_str());
}
