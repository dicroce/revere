#ifndef __r_onvif_r_onvif_push_h
#define __r_onvif_r_onvif_push_h

#include "r_utils/r_macro.h"
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

// ONVIF events pushed by the camera (WS-BaseNotification): Revere subscribes with its own address as the consumer, and
// the camera POSTs a Notify to it for each event. For cameras that don't offer a pull point.
//
// The listener takes input from the network, so it is small and strict. It is bound to one local address (the one the
// camera reaches Revere on, never every interface), in a fixed port range. A request is read only from a camera's own IP,
// to a path made of a random id that only that camera was given, with a head of at most 8 KB and a body of at most 64 KB,
// read under a deadline. Nothing it reads is trusted: the body goes to a parser that has its own limits.
namespace r_onvif
{

constexpr int PUSH_PORT_FIRST = 11040;
constexpr int PUSH_PORT_LAST = 11059;
constexpr size_t PUSH_MAX_HEAD = 8 * 1024;
constexpr size_t PUSH_MAX_BODY = 64 * 1024;
constexpr int PUSH_READ_DEADLINE_MS = 5000;
extern const char* const PUSH_PATH_PREFIX;     // "/onvif/notify/"

// The head of a push request (the request line and headers, up to the blank line), checked.
struct push_head
{
    int status {0};             // 0: fine, read the body. Else the HTTP status to answer with (and close).
    std::string id;             // the subscription id in the path
    size_t content_length {0};
};
R_API push_head parse_push_head(const std::string& head);

// A random 128-bit subscription id, as 32 hex digits.
R_API std::string new_push_id();

// "192.168.1.5" is an IPv4 address and "cam.local" is not.
R_API bool is_ipv4_literal(const std::string& s);

// The local address this computer reaches camera_ip from (the route's, as the OS picks it; nothing is sent). Empty if
// there is no route or camera_ip isn't an IPv4 address. REVERE_ONVIF_PUSH_BIND, when it is an IPv4 address, is the
// answer instead (for a bench, and for tests whose camera is on this computer).
R_API std::string local_ip_for(const std::string& camera_ip);

// Whether a notification (its body) from camera_ip with this id is wanted, called on a listener thread; it must be
// quick and must not throw.
using push_handler = std::function<void(const std::string& body)>;

class r_onvif_push_listener
{
public:
    // The listener on local_ip, shared by every camera reached from that address; started on the first of the ports in
    // [first, last] that binds. Throws if none does.
    R_API static std::shared_ptr<r_onvif_push_listener> acquire(const std::string& local_ip, int first = PUSH_PORT_FIRST, int last = PUSH_PORT_LAST);

    R_API r_onvif_push_listener(const std::string& local_ip, int first, int last);
    R_API ~r_onvif_push_listener() noexcept;
    r_onvif_push_listener(const r_onvif_push_listener&) = delete;
    r_onvif_push_listener& operator=(const r_onvif_push_listener&) = delete;

    // Notifications from camera_ip to .../<id> go to handler.
    R_API void add(const std::string& id, const std::string& camera_ip, push_handler handler);
    R_API void remove(const std::string& id);

    R_API int port() const { return _port; }
    R_API const std::string& local_ip() const { return _local_ip; }
    // What the camera is given as its consumer address.
    R_API std::string url(const std::string& id) const;

    // The counts of what it turned away, for tests and the log.
    R_API size_t rejected() const;

private:
    struct impl;
    void _serve(void* conn);

    std::string _local_ip;
    int _port {0};
    std::unique_ptr<impl> _impl;
};

}

#endif
