"""Client for the recorder's local HTTP API, as the tests use it."""

from __future__ import annotations

import json
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone


class ApiError(RuntimeError):
    """Carries .status (int) and .body (str)."""

    def __init__(self, status: int, body: str) -> None:
        super().__init__(f"HTTP {status}: {body}")
        self.status = status
        self.body = body


def iso(t: datetime.datetime) -> str:
    """UTC time as 'YYYY-MM-DDTHH:MM:SS.mmmZ'."""
    if t.tzinfo is None:
        t = t.replace(tzinfo=timezone.utc)
    return t.strftime("%Y-%m-%dT%H:%M:%S.") + f"{t.microsecond // 1000:03d}Z"


def parse_iso(text: str) -> datetime.datetime:
    """Inverse of iso; returns an aware UTC datetime."""
    # Accepts with or without milliseconds
    if "." in text:
        fmt = "%Y-%m-%dT%H:%M:%S.%fZ"
    else:
        fmt = "%Y-%m-%dT%H:%M:%SZ"
    dt = datetime.strptime(text, fmt)
    return dt.replace(tzinfo=timezone.utc)


def utcnow() -> datetime.datetime:
    """Aware UTC now."""
    return datetime.now(timezone.utc)


class Api:
    def __init__(self, base_url: str, token: str) -> None:
        self.base_url = base_url.rstrip("/")
        self.token = token

    def request(self, method: str, path: str, query: dict | None = None, body: dict | None = None,
                timeout: float = 60.0) -> tuple[int, bytes]:
        """Send HTTP request; returns (status, body bytes)."""
        url = self.base_url + path
        if query:
            encoded = urllib.parse.urlencode(query)
            url += "?" + encoded

        headers = {"Authorization": f"Bearer {self.token}"}
        data = None
        if body is not None:
            data = json.dumps(body).encode("utf-8")
            headers["Content-Type"] = "application/json"

        req = urllib.request.Request(url, data=data, headers=headers, method=method)
        try:
            resp = urllib.request.urlopen(req, timeout=timeout)
            return (resp.status, resp.read())
        except urllib.error.HTTPError as e:
            # An error status is still an answer; only a failed connection raises.
            return (e.code, e.read())
        except Exception as e:
            raise ApiError(0, str(e)) from e

    def get_json(self, path: str, **query) -> object:
        """GET request; raises ApiError unless status is 200."""
        status, body = self.request("GET", path, query=query)
        if status != 200:
            raise ApiError(status, body.decode("utf-8", errors="replace"))
        return json.loads(body)

    def post_json(self, path: str, body: dict) -> object:
        """POST request; raises ApiError unless status is 200."""
        status, resp_body = self.request("POST", path, body=body)
        if status != 200:
            raise ApiError(status, resp_body.decode("utf-8", errors="replace"))
        return json.loads(resp_body)

    def picture(self, camera_id: str, width: int = 640, behind_s: float = 3.0) -> bytes | None:
        """A JPEG of what the recorder has recorded of the camera a moment ago, or None.

        A snapshot is taken from the recorder, never from the fake camera: a second RTSP client on the fake camera
        makes its media seek, which resets its timeline under the recorder that is recording it.
        """
        at = utcnow() - datetime.timedelta(seconds=behind_s)
        try:
            status, body = self.request("GET", "/jpg", query={"camera_id": camera_id, "start_time": iso(at),
                                                              "width": width, "height": width * 9 // 16}, timeout=20.0)
        except ApiError:
            return None
        return body if status == 200 and body[:2] == b"\xff\xd8" else None

    def cameras(self) -> list[dict]:
        """GET /cameras -> the 'cameras' list."""
        data = self.get_json("/cameras")
        return data.get("cameras", [])

    def camera(self, name: str) -> dict | None:
        """Entry whose 'camera_name' equals name."""
        for cam in self.cameras():
            if cam.get("camera_name") == name:
                return cam
        return None

    def wait_for_camera(self, name: str, timeout: float = 30.0) -> dict:
        """Poll once a second until camera(name) is not None."""
        deadline = utcnow().timestamp() + timeout
        while True:
            cam = self.camera(name)
            if cam is not None:
                return cam
            if utcnow().timestamp() > deadline:
                raise ApiError(0, f"Timeout waiting for camera '{name}'")
            time.sleep(1.0)

    def add_camera(self, name: str, username: str, password: str, profile_token: str = "main",
                   retention_days: float = 0.02, motion: bool = True) -> dict:
        """Add a discovered camera to the recorder."""
        cam = self.wait_for_camera(name)
        camera_id = cam["id"]

        # Get profiles
        profiles_resp = self.get_json("/camera_profiles", camera_id=camera_id, username=username, password=password)
        profiles = profiles_resp.get("profiles", [])
        tokens = [p["token"] for p in profiles]
        if profile_token not in tokens:
            raise ApiError(0, f"Profile token '{profile_token}' not found")

        # Measure camera
        measure_resp = self.get_json("/measure_camera", camera_id=camera_id, username=username, password=password, profile_token=profile_token)
        job_id = measure_resp["id"]

        # Poll progress
        deadline = utcnow().timestamp() + 60.0
        while True:
            if utcnow().timestamp() > deadline:
                raise ApiError(0, f"Timeout measuring camera {name}")
            prog = self.get_json("/measure_progress", id=job_id)
            if prog.get("failed"):
                raise ApiError(0, f"Measurement failed: {prog.get('error')}")
            if prog.get("percent_complete", 0) >= 100:
                break
            time.sleep(0.5)

        # Get result
        result = self.get_json("/measure_result", id=job_id)
        byte_rate = result.get("byte_rate", 0)
        video_codec = result.get("video_codec", "h264")

        # Configure camera
        config_body = {
            "camera_id": camera_id,
            "byte_rate": byte_rate,
            "retention_days": retention_days,
            "friendly_name": name,
            "do_motion_detection": motion,
            "username": username,
            "password": password,
        }
        self.post_json("/configure_camera", body=config_body)

        return {
            "camera_id": camera_id,
            "byte_rate": byte_rate,
            "video_codec": video_codec,
            "profiles": profiles,
        }

    def remove_camera(self, camera_id: str, delete_files: bool = True) -> dict:
        """POST /remove_camera."""
        body = {"camera_id": camera_id, "delete_files": delete_files}
        return self.post_json("/remove_camera", body=body)

    def contents(self, camera_id: str, start: datetime.datetime, end: datetime.datetime) -> list[tuple[datetime.datetime, datetime.datetime]]:
        """GET /contents; returns sorted list of (start, end) datetimes."""
        query = {
            "camera_id": camera_id,
            "start_time": iso(start),
            "end_time": iso(end),
        }
        data = self.get_json("/contents", **query)
        segments = data.get("segments", [])
        result = []
        for seg in segments:
            s = parse_iso(seg["start_time"])
            e = parse_iso(seg["end_time"])
            result.append((s, e))
        return sorted(result, key=lambda x: x[0])

    def motion_events(self, camera_id: str, start: datetime.datetime, end: datetime.datetime) -> list[dict]:
        """GET /motion_events; returns the 'motion_events' list."""
        query = {
            "camera_id": camera_id,
            "start_time": iso(start),
            "end_time": iso(end),
        }
        data = self.get_json("/motion_events", **query)
        return data.get("motion_events", [])


def gaps(segments: list[tuple[datetime.datetime, datetime.datetime]]) -> list[tuple[datetime.datetime, float]]:
    """For consecutive segments: (end of the earlier one, seconds until the next one starts)."""
    if len(segments) < 2:
        return []
    result = []
    for i in range(len(segments) - 1):
        end_curr = segments[i][1]
        start_next = segments[i + 1][0]
        delta = (start_next - end_curr).total_seconds()
        if delta > 0:
            result.append((end_curr, delta))
    return result
