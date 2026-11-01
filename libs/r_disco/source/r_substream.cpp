#include "r_disco/r_substream.h"
#include "r_utils/r_logger.h"
#include <algorithm>
#include <exception>

using namespace r_disco;
using namespace std;

r_substream_choice r_disco::find_substream(
    const vector<r_onvif::onvif_profile_info>& profiles,
    const r_onvif::onvif_profile_info& main,
    uint16_t main_width,
    uint16_t main_height,
    const r_substream_camera& camera
)
{
    r_substream_choice choice;

    auto candidates = r_onvif::substream_candidates(profiles, main);
    if(candidates.empty())
    {
        choice.reason = "no smaller stream";
        return choice;
    }

    auto tries = min(candidates.size(), (size_t)SUBSTREAM_MAX_PROBES);
    for(size_t i = 0; i < tries; ++i)
    {
        auto& candidate = candidates[i];

        string url;
        try
        {
            url = camera.stream_uri(candidate.token);
        }
        catch(const exception& e)
        {
            R_LOG_INFO("substream: no stream address for profile %s: %s", candidate.token.c_str(), e.what());
        }

        string reason;
        if(url.empty())
            reason = "no stream address";
        else
        {
            auto probe = camera.probe(url);
            auto check = r_pipeline::check_substream(probe, main_width, main_height);
            if(check.ok)
            {
                choice.found = true;
                choice.reason.clear();
                choice.profile = candidate;
                choice.rtsp_url = url;
                choice.probe = probe;
                choice.anamorphic = check.anamorphic;
                return choice;
            }
            reason = check.reason;
        }

        // The best candidate's reason is the one shown.
        if(choice.reason.empty())
            choice.reason = reason;
    }

    return choice;
}
