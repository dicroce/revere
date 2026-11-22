import json
import socket
import threading
import time
from http.client import HTTPConnection

import pytest

# Import the module to access classes and constants
from revere_harness import web


def _find_free_port():
    """Bind to port 0 to let OS pick a free one, then return it."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


@pytest.fixture(scope="module")
def server(tmp_path_factory):
    """Start the web server on a random local port for the duration of the module."""
    harness_dir = tmp_path_factory.mktemp("harness")
    runs_dir = tmp_path_factory.mktemp("runs")

    port = _find_free_port()
    
    state = web._State(harness_dir, runs_dir, port)
    handler_class = type("BoundHandler", (web._Handler,), {"state": state})
    
    server = web.ThreadingHTTPServer(("127.0.0.1", port), handler_class)
    server.daemon_threads = True
    
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    
    # Give it a moment to start listening
    time.sleep(0.1)
    
    yield type('ServerObj', (), {
        'port': port,
        'token': state.token,
        'state': state,
        'harness_dir': harness_dir,
        'runs_dir': runs_dir
    })()
    
    server.shutdown()


def request(server_obj, method, path, *, token=True, headers=None, body=None, raw_body=None, content_length="auto"):
    """Helper to make HTTP requests."""
    conn = HTTPConnection("127.0.0.1", server_obj.port)
    
    full_path = path
    if token:
        sep = "&" if "?" in path else "?"
        full_path += f"{sep}token={server_obj.token}"
    
    hdrs = headers or {}
    if content_length == "auto":
        # Let http.client handle it
        pass
    elif content_length is None:
        # We will manually send without Content-Length later
        pass
    else:
        hdrs["Content-Length"] = str(content_length)

    if body is not None and not isinstance(body, bytes):
        body = json.dumps(body).encode('utf-8')
    
    if raw_body is not None:
        body = raw_body

    try:
        if content_length is None:
            # Manual send to avoid Content-Length header
            conn.putrequest(method, full_path)
            for k, v in hdrs.items():
                conn.putheader(k, v)
            conn.endheaders()
            if body:
                conn.send(body)
        else:
            conn.request(method, full_path, body=body, headers=hdrs)
        
        resp = conn.getresponse()
        status = resp.status
        data = resp.read()
        try:
            parsed = json.loads(data.decode('utf-8'))
        except json.JSONDecodeError:
            parsed = data.decode('utf-8', errors='replace')
        return status, parsed
    finally:
        conn.close()


def test_get_suites_with_token(server):
    """GET /api/suites with valid token returns 200."""
    status, data = request(server, "GET", "/api/suites")
    assert status == 200
    assert "suites" in data


def test_get_suites_without_token(server):
    """GET /api/suites without token returns 403."""
    status, data = request(server, "GET", "/api/suites", token=False)
    assert status == 403


def test_get_suites_evil_host(server):
    """GET /api/suites with evil Host header returns 403."""
    headers = {"Host": f"evil.example:{server.port}"}
    status, data = request(server, "GET", "/api/suites", headers=headers)
    assert status == 403


def test_post_run_smoke(server):
    """POST /api/run with valid Origin and JSON body returns 200."""
    def mock_start(suite, params, keep):
        return "20260101-000000-smoke"
    
    server.state.start = mock_start
    
    origin = f"http://127.0.0.1:{server.port}"
    body = {"suite": "smoke", "params": {}}
    
    status, data = request(server, "POST", "/api/run", headers={"Origin": origin}, body=body)
    
    assert status == 200


def test_post_run_localhost_origin(server):
    """POST /api/run with localhost Origin returns 200."""
    def mock_start(suite, params, keep):
        return "20260101-000000-smoke"
    
    server.state.start = mock_start
    
    origin = f"http://localhost:{server.port}"
    body = {"suite": "smoke", "params": {}}
    
    status, data = request(server, "POST", "/api/run", headers={"Origin": origin}, body=body)
    
    assert status == 200


def test_post_run_no_origin(server):
    """POST /api/run with no Origin header returns 403."""
    def mock_start(suite, params, keep):
        return "20260101-000000-smoke"
    
    server.state.start = mock_start
    
    body = {"suite": "smoke", "params": {}}
    
    status, data = request(server, "POST", "/api/run", headers={}, body=body)
    
    assert status == 403


def test_post_run_wrong_origin_port(server):
    """POST /api/run with wrong port Origin returns 403."""
    def mock_start(suite, params, keep):
        return "20260101-000000-smoke"
    
    server.state.start = mock_start
    
    origin = f"http://127.0.0.1:{server.port + 1}"
    body = {"suite": "smoke", "params": {}}
    
    status, data = request(server, "POST", "/api/run", headers={"Origin": origin}, body=body)
    
    assert status == 403


def test_post_run_foreign_origin(server):
    """POST /api/run with foreign Origin returns 403."""
    def mock_start(suite, params, keep):
        return "20260101-000000-smoke"
    
    server.state.start = mock_start
    
    origin = "https://example.com"
    body = {"suite": "smoke", "params": {}}
    
    status, data = request(server, "POST", "/api/run", headers={"Origin": origin}, body=body)
    
    assert status == 403


def test_post_stop_foreign_origin(server):
    """POST /api/stop with foreign Origin returns 403."""
    origin = "https://example.com"
    
    status, data = request(server, "POST", "/api/stop", headers={"Origin": origin})
    
    assert status == 403


def test_post_run_negative_content_length(server):
    """POST /api/run with Content-Length -5 returns 411."""
    # The server checks if length < 0. 
    # We need to send a request that results in the server parsing Content-Length as -5.
    # However, http.client might normalize or reject it. 
    # We use raw_body and manual headers to force this.
    
    origin = f"http://127.0.0.1:{server.port}"
    
    conn = HTTPConnection("127.0.0.1", server.port)
    conn.putrequest("POST", "/api/run?token=" + server.token)
    conn.putheader("Host", f"127.0.0.1:{server.port}")
    conn.putheader("Origin", origin)
    conn.putheader("Content-Length", "-5")
    conn.endheaders()
    
    resp = conn.getresponse()
    status = resp.status
    conn.close()
    
    assert status == 411


def test_post_run_no_content_length(server):
    """POST /api/run with no Content-Length header returns 411."""
    origin = f"http://127.0.0.1:{server.port}"
    
    conn = HTTPConnection("127.0.0.1", server.port)
    conn.putrequest("POST", "/api/run?token=" + server.token)
    conn.putheader("Host", f"127.0.0.1:{server.port}")
    conn.putheader("Origin", origin)
    # No Content-Length header
    conn.endheaders()
    
    resp = conn.getresponse()
    status = resp.status
    conn.close()
    
    assert status == 411


def test_post_run_body_too_large(server):
    """POST /api/run with body larger than MAX_BODY returns 413."""
    origin = f"http://127.0.0.1:{server.port}"
    large_body = b"x" * (web.MAX_BODY + 1)
    
    status, data = request(server, "POST", "/api/run", headers={"Origin": origin}, raw_body=large_body, content_length=str(len(large_body)))
    
    assert status == 413


def test_get_run_file_redaction(server):
    """GET run file returns redacted credentials."""
    # Create the run folder and console.log manually
    run_id = "20260101-000000-smoke"
    run_dir = server.runs_dir / run_id
    run_dir.mkdir(parents=True, exist_ok=True)
    
    console_content = "open rtsp://admin:hunter2@10.0.0.9/main\n"
    (run_dir / "console.log").write_text(console_content)
    
    # The route is /api/runs/<id>/file/console.log
    status, data = request(server, "GET", f"/api/runs/{run_id}/file/console.log")
    
    assert status == 200
    assert "rtsp://***:***@10.0.0.9/main" in data
    assert "hunter2" not in data

def test_other_bind_address_is_refused_outside_the_container(monkeypatch, tmp_path):
    """Only the harness container may listen on an address other than loopback."""
    monkeypatch.delenv("REVERE_HARNESS_CONTAINER", raising=False)
    assert web.serve(tmp_path, tmp_path, port=0, bind="0.0.0.0") == 2


def test_out_folder_can_be_moved(monkeypatch, tmp_path):
    """REVERE_HARNESS_OUT moves the run folders, for a harness folder that is read-only."""
    from revere_harness import cli
    monkeypatch.setenv("REVERE_HARNESS_OUT", str(tmp_path))
    assert cli.resolve_out_dir(None, "smoke").parent == tmp_path
    monkeypatch.delenv("REVERE_HARNESS_OUT")
    assert cli.resolve_out_dir(None, "smoke").parent == cli.HARNESS_DIR / "out"
