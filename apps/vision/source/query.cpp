
#include "query.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/3rdparty/json/json.h"
#include "r_http/r_client_request.h"
#include "r_http/r_client_response.h"
#include <map>

using namespace vision;
using namespace r_http;
using namespace r_utils;
using namespace std;
using namespace std::chrono;
using json = nlohmann::json;

vector<sidebar_list_ui_item> vision::query_cameras(const string& ip_address)
{
    vector<sidebar_list_ui_item> cameras;

    r_socket sok;
    sok.set_io_timeout(10000);
    sok.connect(ip_address, 10080);

    r_client_request req(ip_address, 10080);
    req.set_uri(r_uri("/cameras"));
    req.write_request(sok);

    r_client_response resp;
    resp.read_response(sok);

    auto response_txt = resp.get_body_as_string();

    if(response_txt.is_null())
        R_THROW(("Failed to get cameras from Revere."));

    auto j = json::parse(response_txt.value());

    for(auto camera : j["cameras"])
    {
        if(camera["state"] == "assigned")
        {
            sidebar_list_ui_item item;
            item.label = camera["friendly_name"].get<string>();
            item.sub_label = camera["ipv4"].get<string>();
            item.camera_id = camera["id"].get<string>();
            item.do_motion_detection = camera["do_motion_detection"].get<bool>();
            cameras.push_back(item);
        }
    }

    return cameras;
}

vector<uint8_t> vision::query_key(const string& ip_address, const string& camera_id, const string& start_time)
{
    r_socket sok;
    sok.set_io_timeout(10000);

    sok.connect(ip_address, 10080);

    r_client_request req(ip_address, 10080);
    req.set_uri("/key_frame?camera_id=" + camera_id + "&start_time=" + start_time);
    req.write_request(sok);

    r_client_response response;
    response.read_response(sok);

    sok.close();

    return response.release_body();
}

vector<segment> vision::query_segments(const configure_state& cs, const std::string& camera_id, const system_clock::time_point& start, const system_clock::time_point& end)
{
    auto maybe_revere_ipv4 = cs.get_revere_ipv4();
    if(maybe_revere_ipv4.is_null())
        R_THROW(("Revere IPv4 address not set."));

    r_socket sok;
    sok.set_io_timeout(10000);
    sok.connect(maybe_revere_ipv4.value(), 10080);

    r_client_request req(maybe_revere_ipv4.value(), 10080);
    auto uri = r_string_utils::format("/contents?camera_id=%s&start_time=%s&end_time=%s", camera_id.c_str(), r_time_utils::tp_to_iso_8601(start, false).c_str(), r_time_utils::tp_to_iso_8601(end, false).c_str());
    req.set_uri(uri);
    req.write_request(sok);

    r_client_response resp;
    resp.read_response(sok);
    sok.close();

    if(resp.get_status() != 200)
        R_THROW(("Could not query segments."));

    auto doc = resp.get_body_as_string().value();

    auto j = json::parse(doc);

    vector<segment> result;
    result.reserve(j["segments"].size());
    for(auto s : j["segments"])
    {
        segment seg;
        seg.start = r_time_utils::iso_8601_to_tp(s["start_time"]);
        seg.end = r_time_utils::iso_8601_to_tp(s["end_time"]);
        result.push_back(seg);
    }

    return result;
}

vector<motion_event> vision::query_motion_events(const configure_state& cs, const string& camera_id, const system_clock::time_point& start, const system_clock::time_point& end)
{
    auto maybe_revere_ipv4 = cs.get_revere_ipv4();
    if(maybe_revere_ipv4.is_null())
        R_THROW(("Revere IPv4 address not set."));

    r_socket sok;
    sok.set_io_timeout(10000);
    sok.connect(maybe_revere_ipv4.value(), 10080);

    r_client_request req(maybe_revere_ipv4.value(), 10080);
    auto uri = r_string_utils::format("/motion_events?camera_id=%s&start_time=%s&end_time=%s", camera_id.c_str(), r_time_utils::tp_to_iso_8601(start, false).c_str(), r_time_utils::tp_to_iso_8601(end, false).c_str());
    req.set_uri(uri);
    req.write_request(sok);

    r_client_response resp;
    resp.read_response(sok);
    sok.close();

    if(resp.get_status() != 200)
    {
        R_LOG_ERROR("Motion events query failed with status %d", resp.get_status());
        R_THROW(("Could not query motion events."));
    }

    auto response_body = resp.get_body_as_string().value();
    
    auto j = json::parse(response_body);

    //{"avg_motion":0,"end_time":"2022-08-02T06:38:07.000","motion":96,"start_time":"2022-08-02T06:37:55.000","stddev":1}

    vector<motion_event> result;
    result.reserve(j["motion_events"].size());
    for(auto s : j["motion_events"])
    {
        motion_event me;
        me.start = r_time_utils::iso_8601_to_tp(s["start_time"]);
        me.end = r_time_utils::iso_8601_to_tp(s["end_time"]);
        me.motion = s["motion"].get<uint8_t>();
        me.stddev = s["stddev"].get<uint8_t>();
        result.push_back(me);
    }

    return result;
}


vector<analytics_event> vision::query_analytics(const configure_state& cs, const string& camera_id, const system_clock::time_point& start, const system_clock::time_point& end, const string& stream_tag)
{
    auto maybe_revere_ipv4 = cs.get_revere_ipv4();
    if(maybe_revere_ipv4.is_null())
        R_THROW(("Revere IPv4 address not set."));

    r_socket sok;
    sok.set_io_timeout(10000);
    sok.connect(maybe_revere_ipv4.value(), 10080);

    // Build query string
    string query = "/analytics?camera_id=" + camera_id + 
                   "&start_time=" + r_time_utils::tp_to_iso_8601(start, true) + 
                   "&end_time=" + r_time_utils::tp_to_iso_8601(end, true);
    
    // Add stream_tag if provided
    if(!stream_tag.empty()) {
        query += "&stream_tag=" + stream_tag;
    }
    

    r_client_request req(maybe_revere_ipv4.value(), 10080);
    req.set_uri(query);
    req.write_request(sok);

    r_client_response resp;
    resp.read_response(sok);

    sok.close();

    if(resp.get_status() != 200)
    {
        R_LOG_ERROR("Analytics query failed with status %d", resp.get_status());
        R_THROW(("Could not query analytics."));
    }

    auto response_body = resp.get_body_as_string().value();
    auto j = json::parse(response_body);

    vector<analytics_event> result;
    result.reserve(j["analytics"].size());

    for(auto entry : j["analytics"])
    {
        analytics_event ae;
        ae.motion_start_time = r_time_utils::iso_8601_to_tp(entry["motion_start_time"]);
        ae.motion_end_time = r_time_utils::iso_8601_to_tp(entry["motion_end_time"]);
        ae.total_detections = entry["total_detections"].get<int>();

        for(auto det : entry["detections"])
        {
            analytics_detection ad;
            ad.class_name = det["class_name"].get<string>();
            ad.confidence = det["confidence"].get<float>();
            ad.timestamp = r_time_utils::iso_8601_to_tp(det["timestamp"]);
            ae.detections.push_back(ad);
        }

        result.push_back(ae);
    }

    return result;
}
