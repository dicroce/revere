#include "r_vss/r_rtsp_logins.h"
#include "r_vss/r_rtsp_auth.h"
#include "r_vss/r_ws_auth.h"
#include "r_utils/r_logger.h"

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <algorithm>

using namespace r_vss;
using namespace r_utils;
using namespace std;

namespace
{

// The token field that names the login a request came in under.
constexpr const char* LOGIN_FIELD = "revere.login";

struct login_entry
{
    string name;
    vector<GWeakRef*> sessions;    // weak: a session the server drops needs no help from us
};

void _free_refs(login_entry& e)
{
    for(auto w : e.sessions)
    {
        g_weak_ref_clear(w);
        delete w;
    }
    e.sessions.clear();
}

}

struct r_rtsp_logins::state
{
    GstRTSPServer* server {nullptr};
    GstRTSPAuth* auth {nullptr};
    size_t cap {0};
    gulong connected_id {0};
    mutable mutex lock;
    map<string, login_entry> logins;

    ~state()
    {
        for(auto& l : logins)
            _free_refs(l.second);
    }
};

namespace
{

typedef shared_ptr<r_rtsp_logins::state> state_ptr;

// A SETUP under a login. The token is only present now (a PLAY carries none), so this is where the session is
// remembered. A login that is already gone gets its session dropped at once (a request that was past authentication
// when the login was removed).
void _on_setup(GstRTSPClient*, GstRTSPContext* ctx, gpointer user_data)
{
    if(!ctx || !ctx->token || !ctx->session)
        return;
    auto login = gst_rtsp_token_get_string(ctx->token, LOGIN_FIELD);
    if(!login)
        return;    // the shared login

    auto st = *(state_ptr*)user_data;
    bool known = false;
    {
        lock_guard<mutex> g(st->lock);
        auto it = st->logins.find(login);
        if(it != st->logins.end())
        {
            known = true;
            auto& sessions = it->second.sessions;
            bool seen = false;
            for(auto w : sessions)
            {
                auto s = g_weak_ref_get(w);
                if(s)
                {
                    seen = seen || s == (gpointer)ctx->session;
                    g_object_unref(s);
                }
            }
            if(!seen)
            {
                // Sessions the server has dropped since: forgotten as new ones come.
                sessions.erase(remove_if(sessions.begin(), sessions.end(), [](GWeakRef* w) {
                    auto s = g_weak_ref_get(w);
                    if(s)
                    {
                        g_object_unref(s);
                        return false;
                    }
                    g_weak_ref_clear(w);
                    delete w;
                    return true;
                }), sessions.end());
                auto w = new GWeakRef;
                g_weak_ref_init(w, ctx->session);
                sessions.push_back(w);
            }
        }
    }
    if(!known && ctx->server)
    {
        auto pool = gst_rtsp_server_get_session_pool(ctx->server);
        if(pool)
        {
            gst_rtsp_session_pool_remove(pool, ctx->session);
            g_object_unref(pool);
        }
    }
}

void _on_client_connected(GstRTSPServer*, GstRTSPClient* client, gpointer user_data)
{
    // The client holds its own copy of the shared state until it closes.
    g_signal_connect_data(client, "setup-request", G_CALLBACK(_on_setup), new state_ptr(*(state_ptr*)user_data),
                          [](gpointer d, GClosure*) { delete (state_ptr*)d; }, (GConnectFlags)0);
}

// Stops what a session is playing.
GstRTSPFilterResult _stop_media(GstRTSPSession*, GstRTSPSessionMedia* sm, gpointer)
{
    gst_rtsp_session_media_set_state(sm, GST_STATE_NULL);
    return GST_RTSP_FILTER_REMOVE;
}

}

r_rtsp_logins::r_rtsp_logins(GstRTSPServer* server, GstRTSPAuth* auth, size_t cap) :
    _state(make_shared<state>())
{
    _state->server = server;
    _state->auth = auth;
    _state->cap = cap;
    // The server's copy of the pointer is freed when the server drops the handler.
    _state->connected_id = g_signal_connect_data(server, "client-connected", G_CALLBACK(_on_client_connected), new state_ptr(_state),
                                                 [](gpointer d, GClosure*) { delete (state_ptr*)d; }, (GConnectFlags)0);
}

r_rtsp_logins::~r_rtsp_logins() noexcept
{
    try
    {
        // Logins and sessions go with the server; stop new clients from reaching this.
        if(_state->connected_id)
            g_signal_handler_disconnect(_state->server, _state->connected_id);
        lock_guard<mutex> g(_state->lock);
        for(auto& l : _state->logins)
            gst_rtsp_auth_remove_digest(_state->auth, l.first.c_str());
        _state->logins.clear();
    }
    catch(...)
    {
    }
}

r_nullable<r_rtsp_logins::new_login> r_rtsp_logins::add(const string& name)
{
    lock_guard<mutex> g(_state->lock);
    if(_state->logins.size() >= _state->cap)
        return r_nullable<new_login>();

    string username;
    do
    {
        username = "c-" + r_ws_auth::generate_token().substr(0, 12);
    } while(_state->logins.count(username) > 0 || username == r_rtsp_auth::RTSP_USER);

    new_login created;
    created.username = username;
    created.password = r_ws_auth::generate_token();

    // The viewer role as the shared login has it, and the name that finds its sessions again.
    auto token = gst_rtsp_token_new(GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING, r_rtsp_auth::VIEWER_ROLE,
                                    LOGIN_FIELD, G_TYPE_STRING, username.c_str(), NULL);
    gst_rtsp_auth_add_digest(_state->auth, username.c_str(), created.password.c_str(), token);
    gst_rtsp_token_unref(token);

    login_entry e;
    e.name = name;
    _state->logins[username] = std::move(e);

    r_nullable<new_login> result;
    result.set_value(created);
    return result;
}

bool r_rtsp_logins::remove(const string& username)
{
    vector<GWeakRef*> sessions;
    {
        lock_guard<mutex> g(_state->lock);
        auto it = _state->logins.find(username);
        if(it == _state->logins.end())
            return false;
        // First, so nothing new gets in under it.
        gst_rtsp_auth_remove_digest(_state->auth, username.c_str());
        sessions = std::move(it->second.sessions);
        _state->logins.erase(it);
    }

    // Then the streams already playing under it, which removing the login alone doesn't touch.
    auto pool = gst_rtsp_server_get_session_pool(_state->server);
    for(auto w : sessions)
    {
        auto session = (GstRTSPSession*)g_weak_ref_get(w);
        if(session)
        {
            auto stopped = gst_rtsp_session_filter(session, _stop_media, NULL);
            g_list_free_full(stopped, g_object_unref);
            if(pool)
                gst_rtsp_session_pool_remove(pool, session);
            g_object_unref(session);
        }
        g_weak_ref_clear(w);
        delete w;
    }
    if(pool)
        g_object_unref(pool);
    return true;
}

vector<r_rtsp_logins::login_info> r_rtsp_logins::list() const
{
    lock_guard<mutex> g(_state->lock);
    vector<login_info> out;
    for(const auto& l : _state->logins)
        out.push_back({l.first, l.second.name});
    return out;
}
