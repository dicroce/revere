"""Flags resource growth: fits a line to the second half of a run and compares its slope with a threshold."""

from __future__ import annotations

from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    pass

# Private memory, not resident: mapped recording files make resident memory grow by design.
THRESHOLDS: dict[str, float] = {"private_mb": 2.0, "threads": 0.5, "fds": 0.5}
# Smaller rises than these, from the start to the end of the judged part, are taken as noise.
MIN_RISE: dict[str, float] = {"private_mb": 16.0, "threads": 8.0, "fds": 16.0}


def slope_per_minute(points: list[tuple[float, float]]) -> float:
    """Least-squares slope of value over time (seconds), times 60.

    Returns 0.0 for fewer than 2 points or no time spread.
    """
    if len(points) < 2:
        return 0.0

    t_values = [p[0] for p in points]
    v_values = [p[1] for p in points]

    min_t = min(t_values)
    max_t = max(t_values)

    if min_t == max_t:
        return 0.0

    n = len(points)
    sum_t = sum(t_values)
    sum_v = sum(v_values)
    sum_tv = sum(t * v for t, v in points)
    sum_t2 = sum(t * t for t in t_values)

    numerator = n * sum_tv - sum_t * sum_v
    denominator = n * sum_t2 - sum_t * sum_t

    if denominator == 0:
        return 0.0

    slope_per_second = numerator / denominator
    return slope_per_second * 60.0


def _median(values: list[float]) -> float:
    ordered = sorted(values)
    return ordered[len(ordered) // 2]


def analyse(rows: list[dict[str, Any]], min_seconds: float = 60.0, since: float | None = None) -> list[dict[str, Any]]:
    """Analyse resource growth in the second half of a run, or of the part of it after `since`.

    The first half is left out, so growth while the load settles is ignored. If the second half
    covers less than min_seconds, returns [] (too short to judge).
    For each metric in THRESHOLDS returns {"metric", "per_minute": slope, "threshold", "first", "last",
    "flagged"}. "first" and "last" are medians of the first and the last quarter of that half, so one
    odd sample does not decide. A metric is flagged only when its slope is above the threshold and it
    also rose by more than MIN_RISE: a few descriptors or megabytes coming and going is not growth.
    """
    if since is not None:
        rows = [row for row in rows if row["time"] >= since]
    if not rows:
        return []
    times = [row["time"] for row in rows]
    mid_time = (min(times) + max(times)) / 2.0
    half = [row for row in rows if row["time"] >= mid_time]
    if not half or half[-1]["time"] - half[0]["time"] < min_seconds:
        return []

    results = []
    for metric, threshold in THRESHOLDS.items():
        values = [(r["time"], r[metric]) for r in half if metric in r]
        if len(values) < 8:
            continue
        quarter = max(1, len(values) // 4)
        first_val = _median([v for _, v in values[:quarter]])
        last_val = _median([v for _, v in values[-quarter:]])
        slope = slope_per_minute(values)
        results.append({
            "metric": metric,
            "per_minute": slope,
            "threshold": threshold,
            "first": first_val,
            "last": last_val,
            "flagged": slope > threshold and (last_val - first_val) > MIN_RISE[metric],
        })
    return results


def describe(label: str, result: dict[str, Any]) -> str:
    """Describe a growth check result.

    e.g. "recorder private_mb grew 3.2 per minute over the second half of the run (61.0 -> 77.0), threshold 2.0"
    """
    metric = result["metric"]
    slope = result["per_minute"]
    threshold = result["threshold"]
    first_val = result["first"]
    last_val = result["last"]

    return (
        f"{label} {metric} grew {slope:.1f} per minute over the second half of the run "
        f"({first_val:.1f} -> {last_val:.1f}), threshold {threshold}"
    )