"""The check that tells a starved host from a recorder fault."""
import pathlib

from revere_harness import starvation
from revere_harness.reporter import Reporter
from revere_harness.steps import Run

LAG = ("<12>Nov 24 18:18:27 revere: ingest lag[cam1]: 5661 ms behind live for 9990 ms (camera time at 0.22x real time); "
       "reconnecting to drop the backlog (lag reconnect 1 in the last hour; the next is held off 30s)")
OK = "<12>Nov 24 18:19:09 revere: ingest lag[cam4]: 859 ms behind live for 2028 ms (camera time at 0.97x real time); events from this camera are late"


DIP = "<12>Nov 24 20:21:42 revere: ingest lag[cam4]: 1404 ms behind live for 2004 ms (camera time at 0.62x real time); events from this camera are late"


def test_a_reconnect_for_lag_is_told_from_a_dip_the_recorder_rode_out():
    log = "\n".join([LAG, OK, LAG.replace("0.22x", "0.54x"), DIP, "<14>revere: ingest lag[cam3]: camera time jumped back; lag measure re-anchored"])
    assert starvation.slow_cameras(log) == ({"cam1": 0.22}, {"cam4": 0.62})
    assert starvation.slow_cameras(OK) == ({}, {})
    assert starvation.slow_cameras("") == ({}, {})
    # A camera that dipped and was then reconnected to is only reported as reconnected.
    assert starvation.slow_cameras(DIP.replace("cam4", "cam1") + "\n" + LAG) == ({"cam1": 0.22}, {})


def test_a_camera_pinned_on_average_is_named_and_an_idle_one_is_not():
    rows = ["time,label,pid,cpu_percent,rss_mb,private_mb,vms_mb,threads,fds"]
    rows += [f"{t},cam4,1,{150.0},1,1,1,1,1" for t in range(10)]
    rows += [f"{t},cam1,2,{20.0},1,1,1,1,1" for t in range(10)]
    rows += [f"{t},cam5,3,0.0,1,1,1,1,1" for t in range(10)]
    rows += [f"{t},recorder,4,300.0,1,1,1,1,1" for t in range(10)]
    assert starvation.pinned_cameras("\n".join(rows)) == {"cam4": 150.0}


def _run(suite: str):
    return Run(suite, Reporter())


def test_the_run_fails_in_plain_words_when_the_cameras_could_not_keep_up(tmp_path: pathlib.Path):
    (tmp_path / "syslog.log").write_text(LAG + "\n")
    (tmp_path / "resources.csv").write_text("time,label,pid,cpu_percent,rss_mb,private_mb,vms_mb,threads,fds\n")
    run = _run("recording")
    starvation.check(run, tmp_path)
    [r] = run.results
    assert r.status == "failed" and r.name == "the fake cameras kept up with real time"
    assert "cam1 (at 0.22x real time)" in r.detail and "not the recorder's" in r.detail


def test_a_dip_that_was_ridden_out_passes_with_a_note(tmp_path: pathlib.Path):
    (tmp_path / "syslog.log").write_text(DIP + "\n")
    notes: list[str] = []
    run = _run("recording")
    run.reporter.note = notes.append  # type: ignore[method-assign]
    starvation.check(run, tmp_path)
    assert [r.status for r in run.results] == ["passed"]
    assert notes and "cam4 0.62x" in notes[0]


def test_a_run_that_kept_up_passes_and_other_suites_are_left_alone(tmp_path: pathlib.Path):
    (tmp_path / "syslog.log").write_text(OK + "\n")
    run = _run("recording")
    starvation.check(run, tmp_path)
    assert [r.status for r in run.results] == ["passed"]
    smoke = _run("smoke")
    (tmp_path / "syslog.log").write_text(LAG + "\n")
    starvation.check(smoke, tmp_path)
    assert smoke.results == []
