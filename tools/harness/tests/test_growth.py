import pytest
from revere_harness import growth


def make_rows(seconds, step=1.0, private=lambda t: 300.0, threads=lambda t: 100.0, fds=lambda t: 200.0):
    rows = []
    t = 0.0
    while t <= seconds:
        rows.append({"time": t, "private_mb": private(t), "threads": threads(t), "fds": fds(t)})
        t += step
    return rows


def by_metric(results):
    return {r["metric"]: r for r in results}


class TestSlopePerMinute:
    def test_linear_growth(self):
        points = [(t, 5 + 2 * (t / 60)) for t in range(0, 121)]
        assert growth.slope_per_minute(points) == pytest.approx(2.0)

    def test_fewer_than_two_points(self):
        assert growth.slope_per_minute([]) == 0.0
        assert growth.slope_per_minute([(0, 1)]) == 0.0

    def test_same_time(self):
        points = [(0, 1), (0, 2)]
        assert growth.slope_per_minute(points) == 0.0


class TestMinRise:
    def test_private_mb_at_1_min(self):
        assert growth.min_rise("private_mb", 1.0) == pytest.approx(4.0)

    def test_private_mb_at_4_min(self):
        assert growth.min_rise("private_mb", 4.0) == pytest.approx(4.0)

    def test_private_mb_at_15_min(self):
        assert growth.min_rise("private_mb", 15.0) == pytest.approx(15.0)

    def test_fds_at_2_min(self):
        assert growth.min_rise("fds", 2.0) == pytest.approx(4.0)

    def test_fds_at_20_min(self):
        assert growth.min_rise("fds", 20.0) == pytest.approx(5.0)


class TestAnalyseEmptyOrShort:
    def test_no_rows(self):
        assert growth.analyse([]) == []

    def test_second_half_too_short(self):
        rows = make_rows(100, step=1.0)
        assert growth.analyse(rows, min_seconds=60) == []


class TestFlatRun:
    def test_nothing_flagged_and_metrics_covered(self):
        rows = make_rows(600, step=1.0)
        results = growth.analyse(rows)
        metrics = by_metric(results)
        for m in growth.THRESHOLDS:
            assert m in metrics
            assert not metrics[m]["flagged"]


class TestSteadyLeak:
    def test_private_mb_flagged(self):
        rows = make_rows(600, step=1.0, private=lambda t: 300.0 + (t / 60) * 6.0)
        results = growth.analyse(rows)
        metrics = by_metric(results)
        assert metrics["private_mb"]["flagged"]
        assert metrics["private_mb"]["per_minute"] == pytest.approx(6.0, abs=0.5)
        assert not metrics["threads"]["flagged"]
        assert not metrics["fds"]["flagged"]


class TestSlowLeakVsNoiseFloor:
    def test_slow_leak_flagged(self):
        rows = make_rows(480, step=1.0, private=lambda t: 300.0 + (t / 60) * 3.0)
        results = growth.analyse(rows)
        assert by_metric(results)["private_mb"]["flagged"]

    def test_rise_below_noise_floor_not_flagged(self):
        rows = make_rows(180, step=1.0, private=lambda t: 300.0 + (t / 60) * 3.0)
        results = growth.analyse(rows, min_seconds=75)
        assert not by_metric(results)["private_mb"]["flagged"]


class TestStartupGrowthIgnored:
    def test_startup_growth_not_flagged(self):
        rows = make_rows(600, step=1.0, private=lambda t: 300.0 + (200.0 / 250.0) * t if t < 250 else 500.0)
        results = growth.analyse(rows)
        assert not by_metric(results)["private_mb"]["flagged"]


class TestOddSampleDoesNotDecide:
    def test_single_odd_sample_not_flagged(self):
        rows = make_rows(600, step=1.0, fds=lambda t: 260.0 if t == 600 else 200.0)
        results = growth.analyse(rows)
        assert not by_metric(results)["fds"]["flagged"]


class TestStepUpVsSteadyRise:
    def test_step_up_not_flagged(self):
        rows = make_rows(600, step=1.0, fds=lambda t: 203.0 if t >= 450 else 200.0)
        results = growth.analyse(rows)
        assert not by_metric(results)["fds"]["flagged"]

    def test_steady_rise_flagged(self):
        rows = make_rows(600, step=1.0, fds=lambda t: 200.0 + (t / 60) * 3.0)
        results = growth.analyse(rows)
        assert by_metric(results)["fds"]["flagged"]


class TestSinceParameter:
    def test_since_none_flat_second_half(self):
        rows = make_rows(600, step=1.0, private=lambda t: 300.0 + (t / 300.0) * 300.0 if t < 300 else 600.0)
        results = growth.analyse(rows, since=None)
        assert not by_metric(results)["private_mb"]["flagged"]

    def test_since_flat_after_cutoff(self):
        rows = make_rows(600, step=1.0, private=lambda t: 300.0 + (t / 200.0) * 300.0 if t < 200 else 600.0)
        results = growth.analyse(rows, since=200.0)
        assert not by_metric(results)["private_mb"]["flagged"]
        assert by_metric(results)["private_mb"]["first"] == pytest.approx(600.0)


class TestDescribe:
    def test_describe_contains_info(self):
        result = {"metric": "fds", "per_minute": 1.5, "threshold": 0.5, "first": 100.0, "last": 110.0}
        desc = growth.describe("test_label", result)
        assert "test_label" in desc
        assert "fds" in desc
        assert "0.5" in desc
