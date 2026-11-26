#ifndef __r_vss_r_mail_rings_h
#define __r_vss_r_mail_rings_h

#include "r_disco/r_camera.h"
#include "r_utils/r_macro.h"
#include "r_utils/r_nullable.h"
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace r_utils { class r_ssl_server_socket; class r_ssl_server_config; template<class> class r_server_threaded; }

namespace r_vss
{

// A doorbell press reported by email: a Reolink doorbell that doesn't send its press over ONVIF sends an email for it.
struct r_mail_ring
{
    std::string camera_id;
    int64_t press_ms {0};    // when it was pressed, on this box's clock
    int64_t arrival_ms {0};  // when the mail finished arriving
    bool clock_read {false}; // press_ms is the time the mail's subject gives; else the arrival time less 15 s
};

struct r_mail_rings_config
{
    std::string bind_ip;               // the box's LAN address: never every address
    int port {2525};
    std::string password;              // AUTH: the box's own, shown in setup; any user goes with it
    std::string cert_pem;              // STARTTLS: the box's self-signed certificate
    std::string key_pem;
    size_t max_connections {4};
    int command_timeout_ms {30000};    // a command, a DATA line, or the TLS handshake
    int connection_timeout_ms {300000};// a whole connection, however steadily it talks
    size_t max_mail_bytes {2 * 1024 * 1024};
    int mails_per_minute {6};          // per doorbell; more are ignored
    int bad_logins_per_address {6};    // wrong logins from one address in 10 minutes before its AUTH is refused
};

// A small SMTP server for doorbells that report presses by email (Reolink's "Email Alerts"). It takes mail only:
// - over STARTTLS, which it requires before AUTH and before any mail;
// - after AUTH (PLAIN or LOGIN) with the box's password, whatever the user: Reolink's app has no user field and logs in
//   with its sender address. The random password, the LAN address only and the wrong-login limit per address guard it;
// - from the address of a camera that sends rings this way (camera_for_ip); anyone else gets 554 at once.
// It keeps nothing: each mail's headers and text are read for a ring and let go. A ring mail calls on_ring; any other
// mail (a motion alert) is counted, so setup can say to choose "Ring" only.
class r_mail_rings
{
public:
    // The camera whose address this is and that sends rings by email, or null.
    using camera_for_ip_fn = std::function<r_utils::r_nullable<std::string>(const std::string& ip)>;
    using ring_fn = std::function<void(const r_mail_ring&)>;
    // The camera's clock less this box's, in ms (0 when not known): the subject's time is the camera's.
    using clock_offset_fn = std::function<int64_t(const std::string& camera_id)>;
    using now_fn = std::function<int64_t()>;

    struct counts
    {
        uint64_t rings {0};
        uint64_t other_mail {0};       // mail that isn't a ring (a motion alert)
        uint64_t refused {0};          // connections from an address that isn't such a camera's
        uint64_t bad_logins {0};
        uint64_t rate_limited {0};
        int64_t last_ring_ms {0};
        std::string last_error;
    };

    R_API r_mail_rings(r_mail_rings_config config, camera_for_ip_fn camera_for_ip, ring_fn on_ring,
                       clock_offset_fn clock_offset = {}, now_fn now = {});
    R_API ~r_mail_rings() noexcept;

    r_mail_rings(const r_mail_rings&) = delete;
    r_mail_rings& operator=(const r_mail_rings&) = delete;

    R_API void start();
    R_API void stop();
    R_API counts status() const;

private:
    void _serve(r_utils::r_ssl_server_socket& conn);
    void _mail(const std::string& camera_id, const std::string& message);

    r_mail_rings_config _config;
    camera_for_ip_fn _camera_for_ip;
    ring_fn _on_ring;
    clock_offset_fn _clock_offset;
    now_fn _now;
    std::shared_ptr<r_utils::r_ssl_server_config> _tls;
    std::unique_ptr<r_utils::r_server_threaded<r_utils::r_ssl_server_socket>> _server;
    std::thread _accept_thread;
    mutable std::mutex _lock;
    counts _counts;
    std::map<std::string, std::deque<int64_t>> _recent;   // per camera, its mails in the last minute
    std::map<std::string, int64_t> _stranger_logged;     // per address, when a refusal was last logged (recent only)
    std::map<std::string, std::deque<int64_t>> _bad_login_times;   // per address, its wrong logins in the last 10 minutes
};

// Whether a mail is a doorbell's ring: Reolink's subject "Someone Rang the Doorbell at ..." or body "Alarm Event: Visitor".
R_API bool is_ring_mail(const std::string& subject, const std::string& text);
// The time in a ring mail's subject ("... at 2026/11/24 13:05:45"), read in this box's time zone, as ms since the epoch.
R_API r_utils::r_nullable<int64_t> ring_subject_time_ms(const std::string& subject);
// When the press was, on this box's clock: the subject's time corrected by the camera's clock offset, when it falls from
// 60 s before the mail's arrival to 5 s after; else the arrival less 15 s (Reolink's usual delay), with clock_read false.
R_API int64_t ring_press_ms(const r_utils::r_nullable<int64_t>& subject_ms, int64_t clock_offset_ms, int64_t arrival_ms, bool& clock_read);
// A mail's Subject (encoded words decoded) and its text (a base64 body decoded), from the whole message.
R_API void read_mail(const std::string& message, std::string& subject, std::string& text);

// The doorbells that send rings by email: a doorbell (r_disco::camera_kind) set to "reolink_email", with an address.
// By address, to the camera's id: who the listener lets in.
R_API std::map<std::string, std::string> mail_ring_doorbells(const std::vector<r_disco::r_camera>& cameras);

// The box's login for its doorbells' mail, shown in setup: a random 8-character password, short enough to type into a
// doorbell's app, made once and kept in path (one line), readable by its owner only. A login kept another way (before,
// a user and a 20-character password) is made again.
struct r_mail_login { std::string password; };
R_API r_mail_login load_or_make_mail_login(const std::string& path);
// A new password, kept as load_or_make_mail_login keeps it. A doorbell set up with the old one has its mail refused until
// the new one is typed in its app.
R_API r_mail_login make_new_mail_login(const std::string& path);

// The listener's port: 2525, or REVERE_MAIL_RINGS_PORT (a bench).
R_API int mail_rings_port();

// What to type in a Reolink doorbell's email settings (Settings > Email Alerts > Email Settings) for its presses to
// reach this box: the SMTP server is the box's address toward the doorbell, with SSL/TLS on, the box's login, any
// sender, and a recipient the listener takes (it reads every mail from the doorbell, whoever it's to). The sender and
// recipient are real-looking addresses on a domain of ours, which the app accepts (it may refuse .local); no mail goes
// to them.
struct r_mail_setup
{
    std::string server;
    int port {0};
    std::string sender;
    std::string password;
    std::string recipient;
};
R_API r_mail_setup mail_setup_for(const std::string& server, int port, const r_mail_login& login);

// The listener's certificate and key (PEM), made once and kept beside the login: the certificate in cert_path, the key
// in key_path, readable by its owner only.
R_API std::pair<std::string, std::string> load_or_make_mail_cert(const std::string& cert_path, const std::string& key_path);

}

#endif
