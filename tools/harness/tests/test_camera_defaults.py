from revere_harness.camera import CameraSpec


def test_native_defaults_are_the_larger_picture(monkeypatch):
    monkeypatch.delenv("REVERE_HARNESS_CONTAINER", raising=False)
    s = CameraSpec(name="c", index=0)
    assert (s.width, s.height, s.fps, s.bitrate_kbps) == (1280, 720, 15, 1500)


def test_container_defaults_are_the_light_picture(monkeypatch):
    monkeypatch.setenv("REVERE_HARNESS_CONTAINER", "1")
    s = CameraSpec(name="c", index=0)
    assert (s.width, s.height, s.fps, s.bitrate_kbps) == (640, 360, 10, 500)


def test_an_explicit_value_wins_in_both(monkeypatch):
    monkeypatch.setenv("REVERE_HARNESS_CONTAINER", "1")
    assert CameraSpec(name="c", index=0, width=1920, fps=30).width == 1920
    monkeypatch.delenv("REVERE_HARNESS_CONTAINER")
    assert CameraSpec(name="c", index=0, fps=5).fps == 5
