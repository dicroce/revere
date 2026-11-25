"""The suites the harness can run, with the parameters each accepts. Used by the command line and the web page."""

from __future__ import annotations

import math
from typing import Any

# The recorder calls a stream dead after 20 seconds without video or audio and then restarts it
# (r_recording_context::dead in libs/r_vss). The recording suite's drops are sized against this.
DEAD_STREAM_S = 20

SUITES: list[dict] = [
    {
        "name": "smoke",
        "title": "Smoke",
        "description": "Starts a headless recorder in the sandbox, checks it answers, and stops it. About 5 seconds.",
        "params": [],
    },
    {
        "name": "cameras",
        "title": "Fake cameras",
        "description": "Starts six fake ONVIF cameras and proves that streams, discovery and every fault control work. No recorder. About 90 seconds.",
        "params": [],
    },
    {
        "name": "discovery",
        "title": "Discovery",
        "description": "A headless recorder finds every fake camera on the test network. About 15 seconds.",
        "params": [],
    },
    {
        "name": "recording",
        "title": "Recording",
        "description": "Records from four fake cameras; checks that recordings grow and stay continuous, that each deliberate drop shows as a gap, and that scripted motion is detected. About 6 minutes.",
        "params": [
            {
                "name": "long_drop",
                "label": "Long drop, seconds",
                "type": "float",
                "default": 40,
                "min": 25,
                "max": 300,
                "help": "How long the camera is cut off in the three gap tests (cable unplugged, RTSP server stopped, stream stalled). "
                        "Must be well past the recorder's 20 second dead-stream timeout, so the recorder restarts the stream and a gap shows.",
                "recommended": {"value": 40, "why": "twice the 20 s timeout, so a restart and a gap are expected, with room for the reconnect."},
                "warn": [
                    {"op": "<=", "value": 25, "message": "Not well past the 20 s dead-stream timeout: the recorder may not restart the stream, and the gap tests can fail on timing. Use 40 or more."},
                    {"op": ">", "value": 120, "per": 3, "message": "Very long: the three gap tests each wait this long, adding about {minutes} minutes to a run of about 6."},
                ],
            },
            {
                "name": "short_drop",
                "label": "Short drop, seconds",
                "type": "float",
                "default": 8,
                "min": 1,
                "max": 40,
                "help": "An outage the recorder should ride out: shorter than its 20 second dead-stream timeout, so the stream is not restarted and no gap shows.",
                "recommended": {"value": 8, "why": "well under the 20 s timeout, so the stream should not restart."},
                "warn": [
                    {"op": ">=", "value": 15, "message": "Close to the 20 s dead-stream timeout: the reconnect time can push the outage past it, and the recorder will restart the stream."},
                    {"op": ">=", "value": 20, "message": "At or over the 20 s dead-stream timeout: this is a long drop, not a short one; the test expects no restart."},
                ],
            },
        ],
    },
]


SUITES.append({
    "name": "stress",
    "title": "Stress",
    "description": "One stress scenario per run: many cameras, a reconnect storm, rapid add and remove, "
                   "API hammering, a long soak, or a nearly full disk. Checks the recorder keeps working "
                   "and flags growth in its memory, threads and file descriptors.",
    "params": [
        {"name": "scenario", "label": "Scenario", "type": "choice", "default": "many_cameras",
         "choices": ["many_cameras", "reconnect_storm", "add_remove", "api_hammer", "soak", "disk_full"],
         "help": "What to push: camera count, reconnects, add and remove, API requests, duration, or disk space. One scenario per run.",
         "recommended": {"value": "many_cameras", "why": "the usual first check; the others each target one weakness."}},
        {"name": "cameras", "label": "Cameras", "type": "int", "default": 8, "min": 1, "max": 64,
         "help": "Number of fake cameras (api_hammer always uses 2). Each one encodes its own video, so many cost the host CPU as well as the recorder.",
         "recommended": {"value": 8, "why": "fits a 4-CPU container at 640x360 and 10 fps."},
         "warn": [{"op": ">", "value": 16, "message": "Many fake cameras: they encode their own video and can fall behind before the recorder does, which the recorder then answers by reconnecting. Check the report's lagging-camera notes."}]},
        {"name": "minutes", "label": "Minutes", "type": "float", "default": 3, "min": 0.5, "max": 1440,
         "help": "How long the scenario's main step runs. Growth is only judged when this is 2.5 minutes (150 s) or more.",
         "recommended": {"value": 3, "why": "just past the 2.5 minutes growth needs, so the run stays short."},
         "warn": [{"op": "<", "value": 2.5, "message": "Under 2.5 minutes the run does not judge growth in memory, threads or file descriptors."},
                  {"op": ">", "value": 60, "message": "A long soak: the run takes this long, and the container keeps its recordings until it ends."}]},
        {"name": "resolution", "label": "Resolution", "type": "choice", "default": "640x360",
         "choices": ["640x360", "1280x720", "1920x1080"], "help": "Picture size of every camera. Cost grows with the number of pixels.",
         "recommended": {"value": "640x360", "why": "about 4% of a core per camera; 1920x1080 costs about nine times as much."},
         "warn_choice": {"1920x1080": "Nine times the pixels of 640x360: fake cameras on a small host will fall behind, and the recorder reconnects to cameras that lag."}},
        {"name": "fps", "label": "Frames per second", "type": "int", "default": 10, "min": 1, "max": 30,
         "help": "Frame rate of every camera. Encoding cost grows with it.",
         "recommended": {"value": 10, "why": "enough for motion and recording checks at low cost."},
         "warn": [{"op": ">", "value": 15, "message": "A high frame rate makes each fake camera encode more: on a small host they can fall behind."}]},
        {"name": "bitrate_kbps", "label": "Bitrate, kbps", "type": "int", "default": 800, "min": 100, "max": 20000,
         "help": "Target bitrate of every camera; raise it for a high-bitrate run.",
         "recommended": {"value": 800, "why": "plenty for a 640x360 test picture."},
         "warn": [{"op": ">", "value": 8000, "message": "Very high: the recorder writes this much per camera, so the disk and the recorder's CPU are loaded much harder."}]},
        {"name": "clients", "label": "API clients", "type": "int", "default": 8, "min": 1, "max": 64,
         "help": "Parallel clients in the api_hammer scenario.",
         "recommended": {"value": 8, "why": "enough to overlap requests without swamping a small host."},
         "warn": [{"op": ">", "value": 32, "message": "Many parallel clients: failures may come from the host running out of CPU or sockets rather than the recorder."}]},
    ],
})


_OPS = {
    "<": lambda a, b: a < b, "<=": lambda a, b: a <= b, ">": lambda a, b: a > b, ">=": lambda a, b: a >= b,
}


def warnings(name: str, params: dict) -> list[str]:
    """Plain-words cautions about a set of (validated) parameter values; empty when they look sound.

    The page shows the same rules as the values are typed (the "warn" and "warn_choice" entries are data for it).
    """
    suite = get(name)
    found: list[str] = []
    if suite is None:
        return found
    for p in suite["params"]:
        if p["name"] not in params:
            continue
        value = params[p["name"]]
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            for rule in p.get("warn", []):
                if _OPS[rule["op"]](value, rule["value"]):
                    minutes = round(value * rule.get("per", 1) / 60, 1)
                    found.append(f"{p['label']}: {rule['message'].format(minutes=minutes)}")
        elif isinstance(value, str) and value in p.get("warn_choice", {}):
            found.append(f"{p['label']}: {p['warn_choice'][value]}")
    return found


def get(name: str) -> dict | None:
    """Return the suite entry with the given name, or None if not found."""
    for suite in SUITES:
        if suite["name"] == name:
            return suite
    return None


def names() -> list[str]:
    """Return a list of all suite names."""
    return [suite["name"] for suite in SUITES]


def _convert_value(raw: Any, param_def: dict) -> Any:
    """Convert a raw value (usually str) to the type specified in param_def."""
    ptype = param_def["type"]

    if ptype == "bool":
        if isinstance(raw, bool):
            return raw
        if isinstance(raw, str):
            lower = raw.lower()
            if lower in ("true", "1", "yes"):
                return True
            if lower in ("false", "0", "no"):
                return False
        raise ValueError(
            f"Parameter '{param_def['name']}' expects a boolean, got {raw!r}"
        )

    if ptype == "int":
        try:
            val = int(raw)
        except (ValueError, TypeError):
            raise ValueError(
                f"Parameter '{param_def['name']}' expects an integer, got {raw!r}"
            )
        return val

    if ptype == "float":
        try:
            val = float(raw)
        except (ValueError, TypeError):
            raise ValueError(
                f"Parameter '{param_def['name']}' expects a number, got {raw!r}"
            )
        # Check for NaN/Inf which are technically valid floats but often unwanted in config
        if math.isnan(val) or math.isinf(val):
            raise ValueError(
                f"Parameter '{param_def['name']}' cannot be NaN or Infinity"
            )
        return val

    if ptype == "choice":
        choices = param_def.get("choices", [])
        # If raw is already in the list, return it (preserves type)
        if raw in choices:
            return raw
        # Try string match for convenience
        str_raw = str(raw)
        for c in choices:
            if str(c) == str_raw:
                return c
        raise ValueError(
            f"Parameter '{param_def['name']}' must be one of {choices}, got {raw!r}"
        )

    # Fallback for unknown types or strings
    return raw


def validate(name: str, params: dict) -> dict:
    """Validate and convert parameters for a given suite.

    Args:
        name: The name of the suite.
        params: A dictionary of parameter names to values (strings or native types).

    Returns:
        A dictionary with all declared parameters, converted to their correct types.

    Raises:
        ValueError: If the suite is unknown, a param is unknown, or a value is invalid.
    """
    suite = get(name)
    if suite is None:
        raise ValueError(f"Unknown suite: {name}")

    declared_params = {p["name"]: p for p in suite["params"]}
    result: dict[str, Any] = {}

    # First, apply defaults for missing params
    for pname, pdef in declared_params.items():
        if pname not in params:
            result[pname] = pdef["default"]
        else:
            raw_val = params[pname]
            converted = _convert_value(raw_val, pdef)

            # Range checks for numbers
            if pdef["type"] in ("int", "float"):
                min_val = pdef.get("min")
                max_val = pdef.get("max")
                if min_val is not None and converted < min_val:
                    raise ValueError(
                        f"Parameter '{pname}' value {converted} is less than minimum {min_val}"
                    )
                if max_val is not None and converted > max_val:
                    raise ValueError(
                        f"Parameter '{pname}' value {converted} is greater than maximum {max_val}"
                    )

            result[pname] = converted

    # Check for unknown params in input
    for pname in params:
        if pname not in declared_params:
            raise ValueError(f"Unknown parameter for suite '{name}': {pname}")

    return result


def parse_param_args(pairs: list[str]) -> dict:
    """Turn a list of 'key=value' strings into a dictionary.

    Args:
        pairs: List of strings like ["long_drop=60", "x=true"].

    Returns:
        Dictionary mapping keys to string values.

    Raises:
        ValueError: If any item does not contain an '=' character.
    """
    result: dict[str, str] = {}
    for pair in pairs:
        if "=" not in pair:
            raise ValueError(f"Invalid parameter format (missing '='): {pair!r}")
        key, value = pair.split("=", 1)
        result[key] = value
    return result
