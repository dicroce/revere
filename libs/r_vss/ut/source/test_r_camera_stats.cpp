#include "test_r_camera_stats.h"
#include "r_vss/r_camera_stats.h"
#include "r_utils/r_file.h"
#include <chrono>
#include <cstdio>
#include <ctime>
#include <atomic>
#include <future>
#include <thread>
#include <string>
#include <vector>

using namespace r_vss;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

REGISTER_TEST_FIXTURE(test_r_camera_stats);

static motion_event_info _event(system_clock::time_point start)
{
    motion_event_info e {};
    e.start = start;
    e.end = start + seconds(10);
    return e;
}

static r_storage::r_metadata_entry _entry(const string& json)
{
    r_storage::r_metadata_entry e;
    e.stream_tag = "yolov8_motion_plugin";
    e.json_data = json;
    e.timestamp_ms = 0;
    return e;
}

static void _write(const string& path, size_t bytes)
{
    FILE* f = fopen(path.c_str(), "wb");
    string data(bytes, 'x');
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
}

void test_r_camera_stats::test_motion_hour_buckets()
{
    auto day = system_clock::time_point(hours(24 * 20000));
    vector<motion_event_info> events {
        _event(day - minutes(5)),                       // yesterday: not counted
        _event(day),                                    // 00:00
        _event(day + minutes(59)),                      // 00:59
        _event(day + hours(13) + minutes(5)),           // 13:05
        _event(day + hours(23) + minutes(59)),          // 23:59
        _event(day + hours(24)),                        // tomorrow: not counted
    };
    auto b = motion_hour_buckets(events, day);
    RTF_ASSERT_EQUAL(b[0], 2);
    RTF_ASSERT_EQUAL(b[13], 1);
    RTF_ASSERT_EQUAL(b[23], 1);
    int total = 0;
    for(int n : b)
        total += n;
    RTF_ASSERT_EQUAL(total, 4);
}

void test_r_camera_stats::test_count_detections()
{
    vector<r_storage::r_metadata_entry> entries {
        // Two people and a car in one event count once each.
        _entry(R"({"analytics": {"detections": [{"class_name": "person"}, {"class_name": "person"}, {"class_name": "car"}], "total_detections": 3}})"),
        _entry(R"({"analytics": {"detections": [{"class_name": "person"}, {"class_name": "dog"}]}})"),
        _entry(R"({"analytics": {"detections": [{"class_name": "truck"}, {"class_name": "backpack"}]}})"),
        _entry("not json"),
        _entry(R"({"analytics": {"total_detections": 0}})"),
    };
    auto c = count_detections(entries);
    RTF_ASSERT_EQUAL(c.size(), (size_t)4);
    RTF_ASSERT_EQUAL(c[0].first, string("person"));
    RTF_ASSERT_EQUAL(c[0].second, 2);
    RTF_ASSERT_EQUAL(c[1].first, string("vehicle"));
    RTF_ASSERT_EQUAL(c[1].second, 2);
    // Ties keep name order.
    RTF_ASSERT_EQUAL(c[2].first, string("animal"));
    RTF_ASSERT_EQUAL(c[3].first, string("backpack"));

    RTF_ASSERT_EQUAL(detection_group("bus"), string("vehicle"));
    RTF_ASSERT_EQUAL(detection_group("cat"), string("animal"));
    RTF_ASSERT_EQUAL(detection_group("umbrella"), string("umbrella"));
}

void test_r_camera_stats::test_covered_time()
{
    auto t0 = system_clock::time_point(hours(24 * 20000));
    vector<r_vss::segment> segs {
        {t0 - minutes(30), t0 + minutes(10)},           // clipped to the window start
        {t0 + minutes(5), t0 + minutes(20)},            // overlaps the first
        {t0 + minutes(40), t0 + minutes(50)},
        {t0 + minutes(55), t0 + minutes(90)},           // clipped to the window end
    };
    auto covered = covered_time(segs, t0, t0 + minutes(60));
    RTF_ASSERT_EQUAL(covered.count(), (long long)((20 + 10 + 5) * 60));
    RTF_ASSERT_EQUAL(covered_time({}, t0, t0 + minutes(60)).count(), 0LL);
}

void test_r_camera_stats::test_camera_storage_files()
{
    const string base = "./camera_stats_test";
    r_disco::r_camera cam;
    cam.id = "cam";
    cam.record_file_path.set_value(base + ".nts");
    cam.motion_detection_file_path.set_value(base + "_motion.mdb");

    _write(base + ".nts", 1000);
    _write(base + ".mdnts", 200);
    _write(base + ".mdnts.db", 30);
    _write(base + "_motion.mdb", 4);
    // No .mdnts.db-wal, _motion.db: skipped.

    auto files = camera_storage_files("unused", cam);
    RTF_ASSERT_EQUAL(files.size(), (size_t)4);
    RTF_ASSERT_EQUAL(files[0], base + ".nts");
    uint64_t total = 0;
    for(const auto& f : files)
        total += r_fs::file_size(f);
    RTF_ASSERT_EQUAL(total, (uint64_t)1234);

    for(const auto& f : files)
        r_fs::remove_file(f);

    // A camera with no files configured has none.
    r_disco::r_camera none;
    RTF_ASSERT(camera_storage_files("unused", none).empty());
}

void test_r_camera_stats::test_reconnect_log()
{
    auto t0 = system_clock::time_point(hours(24 * 20000));
    reconnect_log log(t0);
    RTF_ASSERT(log.started_at() == t0);
    RTF_ASSERT_EQUAL(log.count_since("a", t0), (size_t)0);

    log.record("a", t0 + hours(1));
    log.record("a", t0 + hours(10));
    log.record("b", t0 + hours(10));
    RTF_ASSERT_EQUAL(log.count_since("a", t0), (size_t)2);
    RTF_ASSERT_EQUAL(log.count_since("a", t0 + hours(5)), (size_t)1);
    RTF_ASSERT_EQUAL(log.count_since("b", t0), (size_t)1);

    // Older than a day is dropped when the next one comes in.
    log.record("a", t0 + hours(26));
    RTF_ASSERT_EQUAL(log.count_since("a", t0), (size_t)2);
}

void test_r_camera_stats::test_local_day_start()
{
    auto now = system_clock::now();
    auto start = local_day_start(now);
    RTF_ASSERT(start <= now);
    RTF_ASSERT(now - start < hours(25));    // 25: a DST day
    time_t t = system_clock::to_time_t(start);
    tm local {};
    localtime_r(&t, &local);
    RTF_ASSERT_EQUAL(local.tm_hour, 0);
    RTF_ASSERT_EQUAL(local.tm_min, 0);
    RTF_ASSERT_EQUAL(local.tm_sec, 0);
}

void test_r_camera_stats::test_formats()
{
    RTF_ASSERT_EQUAL(format_storage(212ULL * 1000 * 1000 * 1000), string("212 GB"));
    RTF_ASSERT_EQUAL(format_storage(840ULL * 1000 * 1000), string("840 MB"));
    RTF_ASSERT_EQUAL(format_storage(2400ULL * 1000 * 1000 * 1000), string("2.4 TB"));

    RTF_ASSERT_EQUAL(format_ago(seconds(20)), string("just now"));
    RTF_ASSERT_EQUAL(format_ago(minutes(4)), string("4 min ago"));
    RTF_ASSERT_EQUAL(format_ago(hours(3) + minutes(20)), string("3 h ago"));
    RTF_ASSERT_EQUAL(format_ago(hours(50)), string("2 d ago"));

    vector<pair<string, int>> c {{"person", 14}, {"vehicle", 3}, {"animal", 1}, {"umbrella", 1}};
    RTF_ASSERT_EQUAL(format_detections(c), string("14 people \xC2\xB7 3 vehicles \xC2\xB7 1 animal"));
    RTF_ASSERT_EQUAL(format_detections({{"person", 1}}), string("1 person"));
    RTF_ASSERT_EQUAL(format_detections({}), string());
    RTF_ASSERT_EQUAL(format_detection_count("person", 7), string("7 people"));
    RTF_ASSERT_EQUAL(format_detection_count("vehicle", 1), string("1 vehicle"));
    RTF_ASSERT_EQUAL(format_detection_count("animal", 0), string("0 animals"));
    RTF_ASSERT_EQUAL(format_detection_count("skis", 2), string("2 skis"));
}

static r_disco::r_camera _camera(const string& id)
{
    r_disco::r_camera c;
    c.id = id;
    return c;
}

// Polls until a read finishes (or 5 s pass).
static bool _poll_until_done(camera_stats_cache& cache, const r_nullable<r_disco::r_camera>& sel, steady_clock::time_point now)
{
    for(int i = 0; i < 500; ++i)
    {
        if(cache.poll(sel, now))
            return true;
        this_thread::sleep_for(milliseconds(10));
    }
    return false;
}

void test_r_camera_stats::test_stats_cache()
{
    atomic<int> calls {0};
    camera_stats_cache cache([&](const r_disco::r_camera& c) {
        camera_stats s;
        s.motion_events_today = ++calls;
        s.detections_today.push_back({c.id, 1});
        return s;
    }, hours(1));

    auto t0 = steady_clock::now();
    r_nullable<r_disco::r_camera> none;
    RTF_ASSERT(!cache.poll(none, t0));
    RTF_ASSERT_EQUAL(calls.load(), 0);

    r_nullable<r_disco::r_camera> a;
    a.set_value(_camera("a"));
    RTF_ASSERT(cache.get("a") == nullptr);
    RTF_ASSERT(_poll_until_done(cache, a, t0));
    RTF_ASSERT(cache.get("a") != nullptr);
    RTF_ASSERT_EQUAL(cache.get("a")->motion_events_today, 1);
    RTF_ASSERT_EQUAL(cache.get("a")->detections_today[0].first, string("a"));

    // Within the interval: no new read.
    RTF_ASSERT(!cache.poll(a, t0 + minutes(1)));
    this_thread::sleep_for(milliseconds(50));
    RTF_ASSERT(!cache.poll(a, t0 + minutes(1)));
    RTF_ASSERT_EQUAL(calls.load(), 1);

    // After it: read again, keeping the old result until the new one lands.
    cache.poll(a, t0 + hours(2));
    RTF_ASSERT(_poll_until_done(cache, a, t0 + hours(2)));
    RTF_ASSERT_EQUAL(cache.get("a")->motion_events_today, 2);

    // Another camera has its own result.
    r_nullable<r_disco::r_camera> b;
    b.set_value(_camera("b"));
    RTF_ASSERT(_poll_until_done(cache, b, t0 + hours(2)));
    RTF_ASSERT_EQUAL(cache.get("b")->detections_today[0].first, string("b"));
    RTF_ASSERT_EQUAL(cache.get("a")->motion_events_today, 2);
}

void test_r_camera_stats::test_stats_cache_one_at_a_time()
{
    promise<void> release;
    auto released = release.get_future().share();
    atomic<int> calls {0};
    camera_stats_cache cache([&](const r_disco::r_camera&) {
        ++calls;
        released.wait();
        return camera_stats();
    }, seconds(0));

    auto now = steady_clock::now();
    r_nullable<r_disco::r_camera> a, b;
    a.set_value(_camera("a"));
    b.set_value(_camera("b"));

    RTF_ASSERT(!cache.poll(a, now));      // starts a, which blocks
    RTF_ASSERT(!cache.poll(b, now));      // a still running: b waits
    RTF_ASSERT(!cache.poll(a, now));
    this_thread::sleep_for(milliseconds(50));
    RTF_ASSERT_EQUAL(calls.load(), 1);

    release.set_value();
    RTF_ASSERT(_poll_until_done(cache, b, now));
    RTF_ASSERT(cache.get("a") != nullptr);
    RTF_ASSERT(cache.get("b") == nullptr);
}
