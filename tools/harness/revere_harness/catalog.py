"""The suites the harness can run, with the parameters each accepts. Used by the command line and the web page."""

from __future__ import annotations

import math
from typing import Any

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
                "help": "How long the camera is cut off in the gap tests.",
            },
            {
                "name": "short_drop",
                "label": "Short drop, seconds",
                "type": "float",
                "default": 8,
                "min": 1,
                "max": 15,
                "help": "An outage shorter than the recorder's dead-stream timeout.",
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
         "help": "What to push: camera count, reconnects, add and remove, API requests, duration, or disk space."},
        {"name": "cameras", "label": "Cameras", "type": "int", "default": 8, "min": 1, "max": 64,
         "help": "Number of fake cameras (api_hammer always uses 2)."},
        {"name": "minutes", "label": "Minutes", "type": "float", "default": 3, "min": 0.5, "max": 1440,
         "help": "How long the scenario's main step runs. Growth is only judged on runs of 2 minutes or more."},
        {"name": "resolution", "label": "Resolution", "type": "choice", "default": "640x360",
         "choices": ["640x360", "1280x720", "1920x1080"], "help": "Picture size of every camera."},
        {"name": "fps", "label": "Frames per second", "type": "int", "default": 10, "min": 1, "max": 30,
         "help": "Frame rate of every camera."},
        {"name": "bitrate_kbps", "label": "Bitrate, kbps", "type": "int", "default": 800, "min": 100, "max": 20000,
         "help": "Target bitrate of every camera; raise it for a high-bitrate run."},
        {"name": "clients", "label": "API clients", "type": "int", "default": 8, "min": 1, "max": 64,
         "help": "Parallel clients in the api_hammer scenario."},
    ],
})


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
