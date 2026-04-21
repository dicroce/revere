# r_onvif / r_disco Robustness Improvements

A brainstormed list of improvements to make camera communication (especially with flaky Reolink devices) more reliable, organized by layer.

---

## HTTP Layer

1. **Retry with exponential backoff** — Currently there are zero retries on network/socket failures. A transient glitch (TCP RST, connection refused while camera is busy) immediately propagates as an error. Add 2–3 retries with backoff (e.g. 1s, 3s, 9s) for socket-level errors and HTTP 5xx responses.

2. **Separate connect vs. read timeouts** — The 30-second timeout applies uniformly. TCP connect on a dead/slow camera can block for the full OS timeout (75+ seconds) before the socket even notifies you. Set an explicit, shorter connect timeout (3–5s) independently from the read timeout.

3. **Keep-alive / connection reuse within a session** — Every SOAP call opens and closes a new TCP connection (`Connection: close`). The camera setup sequence makes 4+ calls (GetSystemDateAndTime → GetCapabilities → GetProfiles → GetStreamUri). On a flaky Reolink, each new TCP handshake is another opportunity to fail. Reusing the socket within a single session reduces failure surface.

4. **Multi-address fallback** — WS-Discovery `XAddrs` can return multiple addresses per camera. The code picks one. If the chosen address refuses connection, it should try the others before giving up.

5. **Classify network errors better** — "Connection refused" (camera port closed/restarting) should be handled differently from "timeout" (camera overloaded/packet loss) vs. "host unreachable" (camera offline). Retrying a refused connection immediately is counterproductive; it makes more sense to back off.

---

## Authentication Layer

6. **Retry GetSystemDateAndTime specifically** — This is the very first call, and it's what sets `_time_offset_seconds`. If it fails (even transiently), digest auth will be miscalculated for the rest of the session and the whole camera setup fails. Retry this call independently with patience before concluding the camera is unreachable.

7. **Refresh time offset on auth failure** — If digest auth fails on an established session, try re-fetching the camera clock before concluding credentials are wrong. Reolink cameras are known to drift or jump their clocks.

8. **Persist negotiated auth mode and SOAP version** — Currently `_auth_mode` and `_soap_ver` are re-negotiated every time an `r_onvif_cam` object is constructed (every 60-70 minute cache expiration, and on every daemon restart). Store the successful combination so the negotiation handshake (which can involve 2–4 failed attempts) is skipped on reconnect.

9. **Auto-try SHA-256 digest when SHA-1 fails** — SHA-256 support exists behind a parameter flag (`use_sha256`), but isn't in the auto-fallback chain. Add it: try SHA-1 digest → SHA-256 digest → PasswordText, in order.

---

## SOAP/ONVIF Protocol Layer

10. **Retry individual SOAP operations, not just the version/auth negotiation** — Right now a single failed HTTP round-trip for GetProfiles means the whole camera setup fails. Add retry at the per-call level for transient SOAP errors (distinct from auth/version errors which have their own fallback).

11. **Profile fallback on GetStreamUri failure** — If `GetStreamUri` fails for the chosen (highest-res) profile, try other returned profiles before giving up. Reolink cameras sometimes have profiles that appear in `GetProfiles` but aren't functional.

12. **Partial capability handling** — If `GetCapabilities` returns a response but the media service `XAddr` is missing or empty, fall back to probing a well-known default path (e.g., `/onvif/media_service`). Several Reolink models use non-standard but predictable paths.

13. **GetServices as fallback** — Newer ONVIF spec uses `GetServices` instead of `GetCapabilities`. Some cameras implement one but not both. Try `GetCapabilities` first, then `GetServices` if the media endpoint isn't found.

14. **More tolerant XML parsing** — Some cameras return valid SOAP but with extra whitespace, incorrect namespace prefixes, or duplicate elements. The `_xpath_local()` fallback already helps, but consider normalizing the response before parsing (e.g., stripping BOM, trimming, handling encoding declarations).

15. **Detect and handle "camera busy" SOAP faults** — Some cameras return specific faults when a resource is temporarily locked (e.g., another ONVIF client is talking to them). Parse these and retry after a short wait rather than treating them as permanent failures.

---

## Discovery Layer

16. **Send multiple WS-Discovery probes per interface** — Multicast UDP is unreliable. Currently one probe is sent per interface. Sending 2–3 probes with short delays (100–200ms apart) significantly improves response rates from cameras that have flaky network stacks.

17. **Unicast probe to known-good IPs** — For cameras already in the database (previously discovered), send a unicast WS-D probe or a direct HTTP probe to their last-known IP as a supplement to multicast. This catches cameras that stop responding to multicast after a reboot.

18. **Configurable discovery timeout** — The 5-second receive timeout is fixed. On congested or bridged networks, cameras can take longer to respond. Expose this as a configuration parameter.

19. **Graceful handling of malformed WS-D responses** — If one camera returns a garbled ProbeMatch, it currently could abort processing of the whole response batch. Wrap per-camera response parsing in a try/catch and log-and-skip bad ones.

---

## Session / Retry Management

20. **Progressive retry schedule for camera setup** — When a full setup sequence fails, the camera has to wait for the next 60-second poll tick. Add a shorter retry schedule for newly discovered cameras or recently-failed cameras (e.g., retry at 5s, 15s, 60s, 5min, 15min before backing off to normal polling).

21. **Per-camera consecutive failure counter** — Track how many times in a row each camera has failed interrogation. After N failures, reduce retry frequency (stop hammering a struggling camera). After M failures, surface the camera as "unreachable" to the application layer. Reset on success.

22. **Re-negotiate on session failure** — If the cached `_soap_ver`/`_auth_mode` on the `r_onvif_cam` object starts failing (e.g., camera rebooted and its ONVIF stack reset), re-enter the negotiation loop rather than perpetually retrying with the stale values.

23. **Parallelize camera interrogation** — The poll loop processes cameras sequentially. With 4+ cameras, a single slow/unresponsive camera can delay all others by up to 60 seconds (4 calls × 30-second timeouts). Interrogate cameras concurrently with a thread pool or async model.

---

## Data Model / Application Layer

24. **Store last-successful-contact timestamp per camera** — Use this to implement a lightweight "ping" before full re-interrogation. If a camera was working 5 minutes ago, try `GetSystemDateAndTime` first; only do full profile negotiation if that also fails.

25. **Periodic RTSP health check** — The RTSP URL is fetched and cached for 60–70 minutes. A cheaper way to keep it valid is to periodically send a RTSP DESCRIBE (already done once at setup) and only do the full ONVIF re-negotiation if DESCRIBE fails.

26. **Surface camera-specific quirk flags** — Some Reolink models consistently need PasswordText, or always need a specific profile token, or always have a 5-second clock skew. Store these as per-camera flags in the database so they're applied immediately on reconnect without going through the full negotiation waterfall.

27. **Expose failure reason to application layer** — Currently failures are logged but the application probably just sees "camera not streaming." Surfacing structured failure reasons (auth failure, unreachable, SOAP error, RTSP error) allows the UI to give actionable feedback ("check credentials" vs. "camera offline").

---

## Implemented

- **#1** HTTP retry with exponential backoff in `_http_interact` (2 attempts, 300ms delay)
- **#6** Retry `GetSystemDateAndTime` in `r_onvif_cam` constructor (3 attempts, 1s/2s backoff; graceful zero-offset fallback)
- **#8** Persist negotiated SOAP version and auth mode across `r_onvif_cam` instances via `r_onvif_provider::_negotiated_params` map; passed as hints to avoid re-negotiation after cache expiry
- **#16** Send 3 WS-Discovery probes per interface with 150ms spacing in `_discover_on_interface`
- **#20** Retry full camera interrogation sequence in `r_onvif_provider::interrogate_camera` (3 attempts, 3s/9s backoff)
