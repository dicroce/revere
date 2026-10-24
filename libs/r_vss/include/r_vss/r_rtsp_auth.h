#ifndef r_vss_r_rtsp_auth_h
#define r_vss_r_rtsp_auth_h

#include "r_utils/r_macro.h"
#include <string>

typedef struct _GstRTSPAuth GstRTSPAuth;
typedef struct _GstRTSPMediaFactory GstRTSPMediaFactory;

namespace r_vss
{

// Authentication for Revere's RTSP restream server (live and playback video,
// port 8554).
//
// One credential, user RTSP_USER and a random password persisted owner-only at
// <top_dir>/rtsp_password, is accepted with Digest authentication only. RTSP
// here is unencrypted, and Digest never sends the password itself, so watching
// the traffic doesn't reveal it. It is deliberately not an API token: it grants
// video and nothing else. Clients fetch it from the web API (GET
// /rtsp_credentials, which requires a token), so anyone allowed to use Revere's
// API can get it, and nobody else.
//
// gst-rtsp-server checks credentials when a client asks for a stream
// (DESCRIBE/SETUP against a factory's permissions); every factory must be
// passed to allow_viewer() or no one can watch it.
namespace r_rtsp_auth
{

inline constexpr const char* RTSP_USER = "revere";
inline constexpr const char* VIEWER_ROLE = "viewer";

// Loads <top_dir>/rtsp_password, creating it on first use.
R_API std::string load_or_create_password(const std::string& top_dir);

// A new GstRTSPAuth (caller owns the reference) accepting RTSP_USER/password
// by Digest, with the viewer role.
R_API GstRTSPAuth* make_auth(const std::string& password);

// Lets authenticated viewers describe and construct media from factory.
R_API void allow_viewer(GstRTSPMediaFactory* factory);

}

}

#endif
