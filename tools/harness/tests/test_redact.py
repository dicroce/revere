"""Checks for the credential masking."""
import pytest

from revere_harness.redact import redact, redact_obj

CASES = [
    ("rtsp://admin:pw@10.0.0.1:554/main", "rtsp://***:***@10.0.0.1:554/main"),
    ("rtsp://admin:p@ss@10.0.0.1:554/main", "rtsp://***:***@10.0.0.1:554/main"),
    ("http://user@host/path", "http://***@host/path"),
    ("rtsp://10.0.0.1:554/main", "rtsp://10.0.0.1:554/main"),
    ("http://host/a@b", "http://host/a@b"),
    ("at 12:30:05 on 10.0.0.1:554", "at 12:30:05 on 10.0.0.1:554"),
    ("Authorization: Bearer abc.def-123", "Authorization: *** ***"),
    ("sent Bearer abc.def-123 to it", "sent Bearer *** to it"),
    ("header Basic YWRtaW46cHc= seen", "header Basic *** seen"),
    ('{"password": "p\\"w", "name": "cam1"}', '{"password": "***", "name": "cam1"}'),
    ('{"pass": "x", "pwd": "y"}', '{"pass": "***", "pwd": "***"}'),
    ("/camera_profiles?camera_id=1&password=secret&x=1", "/camera_profiles?camera_id=1&password=***&x=1"),
    ("login?user=a&pwd=secret", "login?user=a&pwd=***"),
    ("password: hunter2 was refused", "password: *** was refused"),
    ("two rtsp://a:b@h/1 and rtsp://c:d@h/2", "two rtsp://***:***@h/1 and rtsp://***:***@h/2"),
    ("5 passed, 0 failed", "5 passed, 0 failed"),
    ("bypass=1 compass: north", "bypass=1 compass: north"),
    ("", ""),
]


@pytest.mark.parametrize("text, expected", CASES)
def test_redact(text, expected):
    assert redact(text) == expected


def test_redact_is_stable():
    for text, _ in CASES:
        assert redact(redact(text)) == redact(text)


def test_redact_obj():
    assert redact_obj({"detail": ["rtsp://a:b@h/x", 3], "n": None}) == {"detail": ["rtsp://***:***@h/x", 3], "n": None}
    assert redact(None) is None
