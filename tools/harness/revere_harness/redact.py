"""Masks credentials in text before it is written to the terminal, the event log or a report.

The recorder's error bodies pass exception text through, and that text can hold
a camera URL with its password.
"""
from __future__ import annotations

import re

# scheme://user:password@host  and  scheme://user@host
_URL_USER_PASS = re.compile(r"\b([A-Za-z][A-Za-z0-9+.\-]*://)[^/@\s:]+:[^/@\s]*@")
_URL_USER = re.compile(r"\b([A-Za-z][A-Za-z0-9+.\-]*://)[^/@\s:]+@")
_BEARER = re.compile(r"\b(Bearer)\s+\S+", re.IGNORECASE)
_KEYS = r"password|passwd|token|secret|authorization|api_key"
# "key": "value" as in JSON
_JSON_PAIR = re.compile(r'("(?:%s)"\s*:\s*)"(?:[^"\\]|\\.)*"' % _KEYS, re.IGNORECASE)
# key=value as in a query string
_QUERY_PAIR = re.compile(r"\b((?:%s)=)[^&\s\"']+" % _KEYS, re.IGNORECASE)


def redact(text: str) -> str:
    """Returns the text with credentials replaced by ***."""
    if not isinstance(text, str) or not text:
        return text
    text = _URL_USER_PASS.sub(r"\1***:***@", text)
    text = _URL_USER.sub(r"\1***@", text)
    text = _BEARER.sub(r"\1 ***", text)
    text = _JSON_PAIR.sub(r'\1"***"', text)
    text = _QUERY_PAIR.sub(r"\1***", text)
    return text


def redact_obj(obj: object) -> object:
    """Applies redact to every string inside nested dicts, lists and tuples."""
    if isinstance(obj, str):
        return redact(obj)
    if isinstance(obj, dict):
        return {key: redact_obj(value) for key, value in obj.items()}
    if isinstance(obj, list):
        return [redact_obj(value) for value in obj]
    if isinstance(obj, tuple):
        return tuple(redact_obj(value) for value in obj)
    return obj


if __name__ == "__main__":
    assert redact("rtsp://admin:pw@10.0.0.1:554/main") == "rtsp://***:***@10.0.0.1:554/main"
    assert redact("http://user@host/path") == "http://***@host/path"
    assert redact("rtsp://10.0.0.1:554/main") == "rtsp://10.0.0.1:554/main"
    assert redact("at 12:30:05 on 10.0.0.1:554") == "at 12:30:05 on 10.0.0.1:554"
    assert redact("Authorization: Bearer abc.def-123") == "Authorization: Bearer ***"
    assert redact('{"password": "p\\"w", "name": "cam1"}') == '{"password": "***", "name": "cam1"}'
    assert redact("/camera_profiles?camera_id=1&password=secret&x=1") == "/camera_profiles?camera_id=1&password=***&x=1"
    assert redact("two rtsp://a:b@h/1 and rtsp://c:d@h/2") == "two rtsp://***:***@h/1 and rtsp://***:***@h/2"
    assert redact_obj({"detail": ["rtsp://a:b@h/x", 3], "n": None}) == {"detail": ["rtsp://***:***@h/x", 3], "n": None}
    assert redact("") == "" and redact(None) is None
    print("ok")
