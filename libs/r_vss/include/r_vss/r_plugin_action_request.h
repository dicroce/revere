#ifndef r_vss_r_plugin_action_request_h
#define r_vss_r_plugin_action_request_h

#include "r_vss/r_ws_auth.h"
#include "r_http/r_server_request.h"
#include "r_http/r_server_response.h"
#include "r_utils/r_macro.h"
#include <functional>
#include <string>
#include <vector>

namespace r_vss
{

// The actions a plugin's status message (the {"lines": [...]} it returns from
// system_plugin_get_status_message) offers right now: each line's "action", and for a line with a
// "choice", the choice's "action" followed by each option's "value" (what picking that option runs). The lines in the
// detail of each row of a "list" line count too, however deep the lists go (as far as a person can open them).
R_API std::vector<std::string> plugin_offered_actions(const std::string& status_message);

// What POST /system_plugin_action needs of Revere's plugins; r_ws gives it the stream keeper's.
struct plugin_action_host
{
    std::function<std::vector<std::string>()> loaded;                        // the loaded plugins' names
    std::function<std::string(const std::string&)> status_message;           // a plugin's current status message
    std::function<bool(const std::string&, const std::string&)> run;         // asks for system_plugin_action (r_ws queues it for the UI thread); false if it has none
};

// POST /system_plugin_action {"name": "<plugin>", "action": "<action>"}: asks for a plugin's action to run as its
// panel's button or radio row does on the desktop (the plugin hears it on the UI thread, a moment after the answer). Like Revere's other writes it needs a valid token
// (401 without); a body that isn't {"name", "action"} strings is 400; a plugin that isn't loaded is 404;
// and an action the plugin isn't offering in its status message is 400, so the web can only press what a
// panel shows. Answers {"name", "action"} when it was run.
R_API r_http::r_server_response post_system_plugin_action(const r_http::r_server_request& request, r_ws_auth& auth, const plugin_action_host& host);

}

#endif
