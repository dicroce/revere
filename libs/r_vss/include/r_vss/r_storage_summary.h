#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace r_vss
{

// One camera's recording file: where it is, how much room it holds, and the bytes per second it is receiving now
// (0 when it isn't receiving video).
struct r_storage_camera
{
    std::string dir;
    int64_t file_size{0};
    int64_t byte_rate{0};
};

struct r_storage_summary
{
    // The reserved size of every camera's file: what the recordings take of the disk once the files are allocated.
    int64_t recordings_bytes{0};
    int64_t cameras{0};
    // The days the files hold at today's bitrates, over the cameras that have both; 0 when none do.
    double retention_days_min{0.0};
    double retention_days_max{0.0};
    // The folder most of the reserved space is in: the one whose drive's size and free space are worth reporting.
    // Empty without cameras.
    std::string main_dir;
};

inline r_storage_summary summarize_storage(const std::vector<r_storage_camera>& cams)
{
    r_storage_summary s;
    std::vector<std::pair<std::string, int64_t>> by_dir;
    bool have_days = false;
    for(auto& c : cams)
    {
        const int64_t size = std::max<int64_t>(0, c.file_size);
        s.recordings_bytes += size;
        ++s.cameras;
        auto it = std::find_if(by_dir.begin(), by_dir.end(), [&](const auto& p){ return p.first == c.dir; });
        if(it == by_dir.end())
            by_dir.emplace_back(c.dir, size);
        else
            it->second += size;
        if(size > 0 && c.byte_rate > 0)
        {
            const double days = (double)size / (double)c.byte_rate / 86400.0;
            s.retention_days_min = have_days ? std::min(s.retention_days_min, days) : days;
            s.retention_days_max = have_days ? std::max(s.retention_days_max, days) : days;
            have_days = true;
        }
    }
    int64_t best = -1;
    for(auto& p : by_dir)
        if(p.second > best)
        {
            best = p.second;
            s.main_dir = p.first;
        }
    return s;
}

}
