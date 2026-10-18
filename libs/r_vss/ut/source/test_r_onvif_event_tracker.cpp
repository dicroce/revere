#include "test_r_onvif_event_tracker.h"
#include "r_vss/r_onvif_event_tracker.h"
#include <string>
#include <vector>

using namespace r_vss;
using namespace r_onvif;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_onvif_event_tracker);

static onvif_event _ev(const string& topic, const string& data_name, const string& value,
                       const string& op = "Changed",
                       const vector<pair<string, string>>& source = {{"VideoSourceConfigurationToken", "000"}})
{
    onvif_event e;
    e.topic = topic;
    e.property_operation = op;
    e.source = source;
    e.data = {{data_name, value}};
    return e;
}

void test_r_onvif_event_tracker::test_normalize_topic()
{
    RTF_ASSERT_EQUAL(r_onvif_event_tracker::normalize_topic("tns1:RuleEngine/tt:CellMotionDetector/Motion"), string("RuleEngine/CellMotionDetector/Motion"));
    RTF_ASSERT_EQUAL(r_onvif_event_tracker::normalize_topic("tns1:VideoSource/MotionAlarm"), string("VideoSource/MotionAlarm"));
    RTF_ASSERT_EQUAL(r_onvif_event_tracker::normalize_topic("RuleEngine/MyRuleDetector/PeopleDetect"), string("RuleEngine/MyRuleDetector/PeopleDetect"));
    RTF_ASSERT_EQUAL(r_onvif_event_tracker::normalize_topic(""), string(""));
}

// Reolink: CellMotionDetector IsMotion plus MyRuleDetector classes, values true/false.
void test_r_onvif_event_tracker::test_reolink_motion_and_person()
{
    r_onvif_event_tracker t;

    // The first pull: every topic's current state, all off.
    auto tr = t.process({
        _ev("tns1:RuleEngine/CellMotionDetector/Motion", "IsMotion", "false", "Initialized"),
        _ev("tns1:RuleEngine/MyRuleDetector/PeopleDetect", "State", "false", "Initialized"),
        _ev("tns1:RuleEngine/MyRuleDetector/VehicleDetect", "State", "false", "Initialized")
    });
    RTF_ASSERT(tr.empty());
    RTF_ASSERT_EQUAL(t.motion_topic(), string("RuleEngine/CellMotionDetector"));
    RTF_ASSERT_EQUAL(t.classes().size(), (size_t)2);

    tr = t.process({_ev("tns1:RuleEngine/CellMotionDetector/Motion", "IsMotion", "true")});
    RTF_ASSERT_EQUAL(tr.size(), (size_t)1);
    RTF_ASSERT_EQUAL(tr[0].what, string("motion"));
    RTF_ASSERT(tr[0].active);

    // A repeat of the same state is not a transition.
    tr = t.process({_ev("tns1:RuleEngine/CellMotionDetector/Motion", "IsMotion", "true")});
    RTF_ASSERT(tr.empty());

    tr = t.process({_ev("tns1:RuleEngine/MyRuleDetector/PeopleDetect", "State", "true")});
    RTF_ASSERT_EQUAL(tr.size(), (size_t)1);
    RTF_ASSERT_EQUAL(tr[0].what, string("person"));
    RTF_ASSERT(tr[0].active);

    tr = t.process({
        _ev("tns1:RuleEngine/MyRuleDetector/PeopleDetect", "State", "false"),
        _ev("tns1:RuleEngine/CellMotionDetector/Motion", "IsMotion", "false")
    });
    RTF_ASSERT_EQUAL(tr.size(), (size_t)2);
    RTF_ASSERT_EQUAL(tr[0].what, string("person"));
    RTF_ASSERT(!tr[0].active);
    RTF_ASSERT_EQUAL(tr[1].what, string("motion"));
    RTF_ASSERT(!tr[1].active);
}

// Axis with VMD: one burst raises several topics (values 1/0); only the best
// motion topic counts.
void test_r_onvif_event_tracker::test_axis_burst_uses_one_topic()
{
    r_onvif_event_tracker t;
    vector<pair<string, string>> profile1 = {{"VideoSource", "0"}, {"RuleName", "VMD 4: Profile 1"}};
    vector<pair<string, string>> profile2 = {{"VideoSource", "0"}, {"RuleName", "VMD 4: Profile 2"}};

    auto burst = [&](const string& v) {
        return vector<onvif_event>{
            _ev("tns1:VideoSource/MotionAlarm", "State", v, "Changed", {{"VideoSourceToken", "0"}}),
            _ev("tns1:RuleEngine/MotionRegionDetector/Motion", "State", v, "Changed", profile1),
            _ev("tns1:VideoSource/MotionAlarm", "State", v, "Changed", {{"VideoSourceToken", "1"}}),
            _ev("tns1:RuleEngine/MotionRegionDetector/Motion", "State", v, "Changed", profile2),
            _ev("tnsaxis:CameraApplicationPlatform/VMD/Camera1Profile1", "active", v, "Changed")
        };
    };

    // MotionAlarm comes first and starts the motion; the better topic takes
    // over without a second start. Profile 2 is another source: its own start.
    auto tr = t.process(burst("1"));
    RTF_ASSERT_EQUAL(t.motion_topic(), string("RuleEngine/MotionRegionDetector"));
    RTF_ASSERT_EQUAL(tr.size(), (size_t)2);
    for (auto& x : tr)
        RTF_ASSERT(x.active);

    tr = t.process(burst("0"));
    for (auto& x : tr)
    {
        RTF_ASSERT(!x.active);
        RTF_ASSERT(x.topic.find("MotionRegionDetector") != string::npos);
    }
    RTF_ASSERT_EQUAL(tr.size(), (size_t)2);
    RTF_ASSERT(t.close_all().empty());

    // Later bursts: MotionAlarm is ignored, one start per profile.
    tr = t.process(burst("1"));
    RTF_ASSERT_EQUAL(tr.size(), (size_t)2);
    for (auto& x : tr)
        RTF_ASSERT(x.topic.find("MotionRegionDetector") != string::npos);
}

// The lesser topic arrives first and its motion is still on when the better
// topic shows up: one start, one end, nothing left open.
void test_r_onvif_event_tracker::test_topic_upgrade_mid_motion()
{
    r_onvif_event_tracker t;
    vector<pair<string, string>> src = {{"VideoSource", "0"}};

    auto tr = t.process({_ev("tns1:VideoSource/MotionAlarm", "State", "true", "Changed", src)});
    RTF_ASSERT_EQUAL(tr.size(), (size_t)1);
    RTF_ASSERT(tr[0].active);
    RTF_ASSERT_EQUAL(t.motion_topic(), string("VideoSource/MotionAlarm"));

    tr = t.process({_ev("tns1:RuleEngine/MotionRegionDetector/Motion", "State", "true", "Changed", src)});
    RTF_ASSERT(tr.empty());
    RTF_ASSERT_EQUAL(t.motion_topic(), string("RuleEngine/MotionRegionDetector"));

    // The lesser topic is ignored from now on.
    tr = t.process({_ev("tns1:VideoSource/MotionAlarm", "State", "false", "Changed", src)});
    RTF_ASSERT(tr.empty());

    tr = t.process({_ev("tns1:RuleEngine/MotionRegionDetector/Motion", "State", "false", "Changed", src)});
    RTF_ASSERT_EQUAL(tr.size(), (size_t)1);
    RTF_ASSERT(!tr[0].active);
    RTF_ASSERT(t.close_all().empty());

    // Upgrading with the motion already off ends it.
    r_onvif_event_tracker t2;
    t2.process({_ev("tns1:VideoSource/MotionAlarm", "State", "true", "Changed", src)});
    tr = t2.process({_ev("tns1:RuleEngine/CellMotionDetector/Motion", "IsMotion", "false", "Changed", src)});
    RTF_ASSERT_EQUAL(tr.size(), (size_t)1);
    RTF_ASSERT(!tr[0].active);
    RTF_ASSERT(t2.close_all().empty());
}

// A subscription that dropped mid-motion: the next one's Initialized state
// (off) ends it; one that comes back still on starts nothing new.
void test_r_onvif_event_tracker::test_initialized_corrects_state()
{
    r_onvif_event_tracker t;
    t.process({_ev("tns1:RuleEngine/CellMotionDetector/Motion", "IsMotion", "true")});

    auto tr = t.process({_ev("tns1:RuleEngine/CellMotionDetector/Motion", "IsMotion", "true", "Initialized")});
    RTF_ASSERT(tr.empty());

    tr = t.process({_ev("tns1:RuleEngine/CellMotionDetector/Motion", "IsMotion", "false", "Initialized")});
    RTF_ASSERT_EQUAL(tr.size(), (size_t)1);
    RTF_ASSERT(!tr[0].active);

    // A first pull that finds motion already on starts it.
    r_onvif_event_tracker t2;
    tr = t2.process({_ev("tns1:RuleEngine/CellMotionDetector/Motion", "IsMotion", "true", "Initialized")});
    RTF_ASSERT_EQUAL(tr.size(), (size_t)1);
    RTF_ASSERT(tr[0].active);
}

// Two video sources' motions are separate states.
void test_r_onvif_event_tracker::test_sources_stay_apart()
{
    r_onvif_event_tracker t;
    vector<pair<string, string>> a = {{"VideoSourceConfigurationToken", "A"}};
    vector<pair<string, string>> b = {{"VideoSourceConfigurationToken", "B"}};

    auto tr = t.process({
        _ev("tns1:RuleEngine/CellMotionDetector/Motion", "IsMotion", "true", "Changed", a),
        _ev("tns1:RuleEngine/CellMotionDetector/Motion", "IsMotion", "true", "Changed", b)
    });
    RTF_ASSERT_EQUAL(tr.size(), (size_t)2);
    RTF_ASSERT(tr[0].source != tr[1].source);

    tr = t.process({_ev("tns1:RuleEngine/CellMotionDetector/Motion", "IsMotion", "false", "Changed", a)});
    RTF_ASSERT_EQUAL(tr.size(), (size_t)1);
    RTF_ASSERT_EQUAL(tr[0].source, string("VideoSourceConfigurationToken=A"));

    tr = t.close_all();
    RTF_ASSERT_EQUAL(tr.size(), (size_t)1);
    RTF_ASSERT_EQUAL(tr[0].source, string("VideoSourceConfigurationToken=B"));
}

// The generic Office camera: IVA/IVB classes, "active" for on.
void test_r_onvif_event_tracker::test_close_all()
{
    r_onvif_event_tracker t;
    t.process({
        _ev("tns1:RuleEngine/CellMotionDetector/Motion", "IsMotion", "true"),
        _ev("tns1:UserAlarm/IVA/HumanShapeDetect", "State", "active"),
        _ev("tns1:VehicleAlarm/IVB/VehicleDetect", "State", "false")
    });

    auto tr = t.close_all();
    RTF_ASSERT_EQUAL(tr.size(), (size_t)2);
    bool motion = false, person = false;
    for (auto& x : tr)
    {
        RTF_ASSERT(!x.active);
        RTF_ASSERT(!x.topic.empty());
        motion = motion || x.what == "motion";
        person = person || x.what == "person";
    }
    RTF_ASSERT(motion && person);

    RTF_ASSERT(t.close_all().empty());

    // Closed means a later start is a start again.
    tr = t.process({_ev("tns1:RuleEngine/CellMotionDetector/Motion", "IsMotion", "true")});
    RTF_ASSERT_EQUAL(tr.size(), (size_t)1);
    RTF_ASSERT(tr[0].active);
}

void test_r_onvif_event_tracker::test_unknown_topics_ignored()
{
    r_onvif_event_tracker t;
    auto tr = t.process({
        _ev("tns1:Device/Trigger/DigitalInput", "LogicalState", "true"),
        _ev("tns1:VideoSource/ImageTooDark/AnalyticsService", "State", "true"),
        // A known topic without its data item.
        _ev("tns1:RuleEngine/CellMotionDetector/Motion", "State", "true")
    });
    RTF_ASSERT(tr.empty());
    RTF_ASSERT(t.motion_topic().empty());
    RTF_ASSERT(t.classes().empty());
}
