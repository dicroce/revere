"""Masks credentials in text before it is written to the terminal, the event log or a report.

The recorder's error bodies pass exception text through, and that text can hold
a camera URL with its password.
"""
from __future__ import annotations

import re

# scheme://userinfo@host. The userinfo runs to the last @ before the path, so a
# password that itself holds an @ is covered whole.
_URL_USERINFO = re.compile(r"\b([A-Za-z][A-Za-z0-9+.\-]*://)([^/\s\"'<>]*)@")
_AUTH_SCHEME = re.compile(r"\b(Bearer|Basic)\s+[A-Za-z0-9._~+/=\-]+", re.IGNORECASE)
_KEYS = r"password|passwd|pass|pwd|token|secret|authorization|api_key"
# "key": "value" as in JSON
_JSON_PAIR = re.compile(r'("(?:%s)"\s*:\s*)"(?:[^"\\]|\\.)*"' % _KEYS, re.IGNORECASE)
# key=value as in a query string, and key: value as in a log line or header
_PLAIN_PAIR = re.compile(r"(?<![A-Za-z0-9_])((?:%s)\s*[=:]\s*)(?!\*\*\*)(?![\"'])[^&\s\"',;]+" % _KEYS, re.IGNORECASE)


def _userinfo(match: re.Match) -> str:
    return match.group(1) + ("***:***@" if ":" in match.group(2) else "***@")


def redact(text: str) -> str:
    """Returns the text with credentials replaced by ***."""
    if not isinstance(text, str) or not text:
        return text
    text = _URL_USERINFO.sub(_userinfo, text)
    text = _AUTH_SCHEME.sub(r"\1 ***", text)
    text = _JSON_PAIR.sub(r'\1"***"', text)
    text = _PLAIN_PAIR.sub(r"\1***", text)
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
