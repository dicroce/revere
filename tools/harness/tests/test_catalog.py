"""The suites' parameter guidance: recommended values, and the cautions printed and shown as values are typed."""
import pytest

from revere_harness import catalog


def _params():
    for suite in catalog.SUITES:
        for p in suite["params"]:
            yield suite, p


def test_every_number_has_a_recommendation_and_the_default_is_it():
    for suite, p in _params():
        if p["type"] in ("int", "float", "choice"):
            assert p.get("recommended"), f"{suite['name']}.{p['name']} has no recommended value"
            assert p["recommended"]["value"] == p["default"], f"{suite['name']}.{p['name']}: recommended differs from the default"
            assert len(p["recommended"]["why"]) > 10


def test_defaults_raise_no_cautions():
    for suite in catalog.SUITES:
        assert catalog.warnings(suite["name"], catalog.validate(suite["name"], {})) == []


def test_rules_are_well_formed_and_cover_the_range():
    for suite, p in _params():
        for rule in p.get("warn", []):
            assert rule["op"] in ("<", "<=", ">", ">=")
            assert isinstance(rule["value"], (int, float)) and rule["message"]
            assert "{minutes}" not in rule["message"] or "per" in rule
        for value in p.get("warn_choice", {}):
            assert value in p["choices"]


def test_short_drop_is_warned_when_it_nears_or_passes_the_dead_stream_timeout():
    assert catalog.DEAD_STREAM_S == 20
    words = lambda v: catalog.warnings("recording", {"short_drop": v, "long_drop": 40.0})
    assert words(8.0) == []
    near = words(16.0)
    assert len(near) == 1 and "dead-stream timeout" in near[0]
    over = words(25.0)
    assert any("long drop, not a short one" in w for w in over)
    # Values past the old limit of 15 are accepted now, so the caution can show.
    assert catalog.validate("recording", {"short_drop": "25"})["short_drop"] == 25.0


def test_long_drop_is_warned_when_it_is_not_well_past_the_timeout_or_is_very_long():
    words = lambda v: catalog.warnings("recording", {"short_drop": 8.0, "long_drop": v})
    assert words(40.0) == []
    assert any("well past" in w for w in words(25.0))
    long = words(240.0)
    assert len(long) == 1 and "12.0 minutes" in long[0]


def test_a_choice_can_be_warned_about():
    base = {"scenario": "many_cameras", "cameras": 8, "minutes": 3, "resolution": "640x360", "fps": 10, "bitrate_kbps": 800, "clients": 8}
    assert catalog.warnings("stress", base) == []
    assert any("Nine times" in w for w in catalog.warnings("stress", {**base, "resolution": "1920x1080"}))


def test_unknown_suites_and_missing_values_say_nothing():
    assert catalog.warnings("nope", {"x": 1}) == []
    assert catalog.warnings("recording", {}) == []


@pytest.mark.parametrize("value,expected", [(2.0, True), (2.5, False), (10, False)])
def test_stress_minutes_under_the_growth_threshold(value, expected):
    p = {"scenario": "soak", "cameras": 8, "minutes": value, "resolution": "640x360", "fps": 10, "bitrate_kbps": 800, "clients": 8}
    assert any("2.5 minutes" in w for w in catalog.warnings("stress", p)) is expected
