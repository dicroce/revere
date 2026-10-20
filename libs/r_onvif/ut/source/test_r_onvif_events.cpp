#include "test_r_onvif_events.h"
#include "r_onvif/r_onvif_session.h"
#include "r_utils/r_string_utils.h"
#include <pugixml.hpp>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace r_onvif;

REGISTER_TEST_FIXTURE(test_r_onvif_events);

// Shaped like the responses our cameras sent on 2026-10-17 (Reolink and Axis).

static const char* REOLINK_CREATE =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:wsa5=\"http://www.w3.org/2005/08/addressing\" "
    "xmlns:wsnt=\"http://docs.oasis-open.org/wsn/b-2\" xmlns:tev=\"http://www.onvif.org/ver10/events/wsdl\">"
    "<SOAP-ENV:Body><tev:CreatePullPointSubscriptionResponse>"
    "<tev:SubscriptionReference><wsa5:Address>http://192.168.0.33:8000/onvif/PullSubManager?Idx=537801457</wsa5:Address></tev:SubscriptionReference>"
    "<wsnt:CurrentTime>2026-10-17T15:23:17Z</wsnt:CurrentTime><wsnt:TerminationTime>2026-10-17T15:25:17Z</wsnt:TerminationTime>"
    "</tev:CreatePullPointSubscriptionResponse></SOAP-ENV:Body></SOAP-ENV:Envelope>";

// The reference parameter's prefix is declared on the Envelope, not on the element.
static const char* AXIS_CREATE =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:wsa=\"http://www.w3.org/2005/08/addressing\" "
    "xmlns:wsnt=\"http://docs.oasis-open.org/wsn/b-2\" xmlns:tev=\"http://www.onvif.org/ver10/events/wsdl\" xmlns:dom0=\"http://www.axis.com/2009/event\">"
    "<SOAP-ENV:Body><tev:CreatePullPointSubscriptionResponse>"
    "<tev:SubscriptionReference><wsa:Address>http://192.168.0.29/onvif/services</wsa:Address>"
    "<wsa:ReferenceParameters><dom0:SubscriptionId>17</dom0:SubscriptionId></wsa:ReferenceParameters></tev:SubscriptionReference>"
    "<wsnt:CurrentTime>2022-08-14T14:06:51Z</wsnt:CurrentTime><wsnt:TerminationTime>2022-08-14T14:08:51Z</wsnt:TerminationTime>"
    "</tev:CreatePullPointSubscriptionResponse></SOAP-ENV:Body></SOAP-ENV:Envelope>";

static const char* PULL_MESSAGES =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:wsnt=\"http://docs.oasis-open.org/wsn/b-2\" "
    "xmlns:tev=\"http://www.onvif.org/ver10/events/wsdl\" xmlns:tt=\"http://www.onvif.org/ver10/schema\" xmlns:tns1=\"http://www.onvif.org/ver10/topics\">"
    "<SOAP-ENV:Body><tev:PullMessagesResponse>"
    "<tev:CurrentTime>2026-10-17T15:48:50Z</tev:CurrentTime><tev:TerminationTime>2026-10-17T15:50:50Z</tev:TerminationTime>"
    "<wsnt:NotificationMessage>"
      "<wsnt:Topic Dialect=\"http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet\">tns1:RuleEngine/CellMotionDetector/Motion</wsnt:Topic>"
      "<wsnt:Message><tt:Message UtcTime=\"2026-10-17T15:48:50Z\" PropertyOperation=\"Changed\">"
        "<tt:Source><tt:SimpleItem Name=\"VideoSourceConfigurationToken\" Value=\"000\"/><tt:SimpleItem Name=\"Rule\" Value=\"000\"/></tt:Source>"
        "<tt:Data><tt:SimpleItem Name=\"IsMotion\" Value=\"true\"/></tt:Data>"
      "</tt:Message></wsnt:Message>"
    "</wsnt:NotificationMessage>"
    "<wsnt:NotificationMessage>"
      "<wsnt:Topic Dialect=\"http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet\">\n  tns1:RuleEngine/MotionRegionDetector/Motion\n</wsnt:Topic>"
      "<wsnt:Message><tt:Message UtcTime=\"2022-08-14T18:45:20Z\" PropertyOperation=\"Initialized\">"
        "<tt:Source><tt:SimpleItem Name=\"RuleName\" Value=\"Profile 1\"/></tt:Source>"
        "<tt:Data><tt:SimpleItem Name=\"State\" Value=\"0\"/></tt:Data>"
      "</tt:Message></wsnt:Message>"
    "</wsnt:NotificationMessage>"
    "</tev:PullMessagesResponse></SOAP-ENV:Body></SOAP-ENV:Envelope>";

static const char* PULL_MESSAGES_EMPTY =
    "<?xml version=\"1.0\"?>"
    "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:tev=\"http://www.onvif.org/ver10/events/wsdl\">"
    "<s:Body><tev:PullMessagesResponse><tev:CurrentTime>2026-10-17T15:48:55Z</tev:CurrentTime>"
    "<tev:TerminationTime>2026-10-17T15:50:55Z</tev:TerminationTime></tev:PullMessagesResponse></s:Body></s:Envelope>";

static const char* EVENT_PROPERTIES =
    "<?xml version=\"1.0\"?>"
    "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:tev=\"http://www.onvif.org/ver10/events/wsdl\" "
    "xmlns:wstop=\"http://docs.oasis-open.org/wsn/t-1\" xmlns:tns1=\"http://www.onvif.org/ver10/topics\" xmlns:tt=\"http://www.onvif.org/ver10/schema\">"
    "<s:Body><tev:GetEventPropertiesResponse><wstop:TopicSet>"
      "<tns1:VideoSource><tns1:MotionAlarm wstop:topic=\"true\"><tt:MessageDescription IsProperty=\"true\"><tt:Data/></tt:MessageDescription></tns1:MotionAlarm></tns1:VideoSource>"
      "<tns1:RuleEngine><tns1:CellMotionDetector><tns1:Motion wstop:topic=\"true\"><tt:MessageDescription IsProperty=\"true\"/></tns1:Motion></tns1:CellMotionDetector>"
        "<tns1:MyRuleDetector><tns1:PeopleDetect wstop:topic=\"true\"/></tns1:MyRuleDetector></tns1:RuleEngine>"
    "</wstop:TopicSet></tev:GetEventPropertiesResponse></s:Body></s:Envelope>";

void test_r_onvif_events::test_pull_point_without_reference_parameters()
{
    auto pp = parse_create_pull_point_response(REOLINK_CREATE);
    RTF_ASSERT(pp.address == "http://192.168.0.33:8000/onvif/PullSubManager?Idx=537801457");
    RTF_ASSERT(pp.reference_parameters.empty());
    RTF_ASSERT(pp.termination_time == "2026-10-17T15:25:17Z");
}

void test_r_onvif_events::test_pull_point_reference_parameters_keep_namespaces()
{
    auto pp = parse_create_pull_point_response(AXIS_CREATE);
    RTF_ASSERT(pp.address == "http://192.168.0.29/onvif/services");
    RTF_ASSERT(pp.reference_parameters.size() == 1);

    // Echoed on its own as a SOAP header, the element must carry the
    // declaration of the prefix it inherited from the Envelope.
    pugi::xml_document d;
    RTF_ASSERT(d.load_string(pp.reference_parameters.front().c_str()));
    auto e = d.document_element();
    RTF_ASSERT(string(e.name()) == "dom0:SubscriptionId");
    RTF_ASSERT(string(e.attribute("xmlns:dom0").value()) == "http://www.axis.com/2009/event");
    RTF_ASSERT(string(e.child_value()) == "17");
}

void test_r_onvif_events::test_pull_messages()
{
    auto evs = parse_pull_messages_response(PULL_MESSAGES);
    RTF_ASSERT(evs.size() == 2);

    RTF_ASSERT(evs[0].topic == "tns1:RuleEngine/CellMotionDetector/Motion");
    RTF_ASSERT(evs[0].utc_time == "2026-10-17T15:48:50Z");
    RTF_ASSERT(evs[0].property_operation == "Changed");
    RTF_ASSERT(evs[0].source.size() == 2);
    RTF_ASSERT(evs[0].source[0].first == "VideoSourceConfigurationToken" && evs[0].source[0].second == "000");
    RTF_ASSERT(evs[0].data.size() == 1);
    RTF_ASSERT(evs[0].data[0].first == "IsMotion" && evs[0].data[0].second == "true");

    // Whitespace around the topic is trimmed.
    RTF_ASSERT(evs[1].topic == "tns1:RuleEngine/MotionRegionDetector/Motion");
    RTF_ASSERT(evs[1].property_operation == "Initialized");
    RTF_ASSERT(evs[1].source.size() == 1 && evs[1].source[0].second == "Profile 1");
    RTF_ASSERT(evs[1].data.size() == 1 && evs[1].data[0].first == "State" && evs[1].data[0].second == "0");
}

void test_r_onvif_events::test_pull_messages_empty()
{
    // A pull that timed out with nothing to report.
    RTF_ASSERT(parse_pull_messages_response(PULL_MESSAGES_EMPTY).empty());
}

void test_r_onvif_events::test_event_topics()
{
    auto topics = parse_event_topics(EVENT_PROPERTIES);
    RTF_ASSERT(topics.size() == 3);
    RTF_ASSERT(topics[0] == "VideoSource/MotionAlarm");
    RTF_ASSERT(topics[1] == "RuleEngine/CellMotionDetector/Motion");
    RTF_ASSERT(topics[2] == "RuleEngine/MyRuleDetector/PeopleDetect");
}

void test_r_onvif_events::test_unparseable_responses_throw()
{
    bool threw = false;
    try { parse_create_pull_point_response("not xml <"); } catch (const runtime_error&) { threw = true; }
    RTF_ASSERT(threw);

    threw = false;
    try { parse_create_pull_point_response(PULL_MESSAGES_EMPTY); } catch (const runtime_error&) { threw = true; }  // no address
    RTF_ASSERT(threw);

    threw = false;
    try { parse_pull_messages_response(""); } catch (const runtime_error&) { threw = true; }
    RTF_ASSERT(threw);
}

void test_r_onvif_events::test_packbits()
{
    // Runs, literals, and a run longer than one PackBits block (128).
    vector<uint8_t> in;
    for (int i = 0; i < 300; ++i) in.push_back(0xFF);
    in.push_back(0x01); in.push_back(0x02); in.push_back(0x03);
    in.push_back(0x00); in.push_back(0x00);
    in.push_back(0x7F);
    auto packed = packbits_encode(in);
    RTF_ASSERT(packed.size() < in.size());
    RTF_ASSERT(packbits_decode(packed) == in);

    RTF_ASSERT(packbits_decode(packbits_encode({})).empty());
    vector<uint8_t> one = {0x42};
    RTF_ASSERT(packbits_decode(packbits_encode(one)) == one);
}

void test_r_onvif_events::test_cell_masks()
{
    // Office's (2026-10-20): every cell of its 22 x 18 grid.
    RTF_ASSERT_EQUAL(full_cell_mask(22, 18), string("0P8A8A=="));
    RTF_ASSERT(!cell_mask_empty("0P8A8A==", 22 * 18));

    // No cells: 50 zero bytes.
    vector<uint8_t> zeros(50, 0);
    auto packed = packbits_encode(zeros);
    auto empty = r_utils::r_string_utils::to_base64(packed.data(), packed.size());
    RTF_ASSERT(cell_mask_empty(empty, 22 * 18));

    // One cell, the last one.
    vector<uint8_t> last(50, 0);
    last[49] = 0x10;   // cell 395: bit 3 of byte 49
    packed = packbits_encode(last);
    RTF_ASSERT(!cell_mask_empty(r_utils::r_string_utils::to_base64(packed.data(), packed.size()), 22 * 18));

    // Missing or broken: empty.
    RTF_ASSERT(cell_mask_empty("", 22 * 18));

    // A layout that isn't a multiple of 8 cells pads with zero bits.
    RTF_ASSERT(!cell_mask_empty(full_cell_mask(3, 3), 9));
}

// Office's GetVideoAnalyticsConfigurations response (2026-10-20), trimmed.
static const char* OFFICE_ANALYTICS =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:tt=\"http://www.onvif.org/ver10/schema\" "
    "xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\"><SOAP-ENV:Body><trt:GetVideoAnalyticsConfigurationsResponse>"
    "<trt:Configurations token=\"0001\"><tt:Name>0001</tt:Name><tt:UseCount>2</tt:UseCount>"
    "<tt:AnalyticsEngineConfiguration>"
    "<tt:AnalyticsModule Name=\"MyCellMotion\" Type=\"tt:CellMotionEngine\"><tt:Parameters>"
    "<tt:SimpleItem Name=\"Sensitivity\" Value=\"88\"/>"
    "<tt:ElementItem Name=\"Layout\"><tt:CellLayout Columns=\"22\" Rows=\"18\"><tt:Transformation>"
    "<tt:Translate x=\"-1.000000\" y=\"-1.000000\"/><tt:Scale x=\"-0.090909\" y=\"0.111111\"/>"
    "</tt:Transformation></tt:CellLayout></tt:ElementItem>"
    "</tt:Parameters></tt:AnalyticsModule>"
    "</tt:AnalyticsEngineConfiguration>"
    "<tt:RuleEngineConfiguration>"
    "<tt:Rule Name=\"MyMotionDetector\" Type=\"tt:CellMotionDetector\"><tt:Parameters>"
    "<tt:SimpleItem Name=\"MinCount\" Value=\"8\"/><tt:SimpleItem Name=\"AlarmOnDelay\" Value=\"1000\"/>"
    "<tt:SimpleItem Name=\"AlarmOffDelay\" Value=\"1000\"/><tt:SimpleItem Name=\"ActiveCells\" Value=\"0P8A8A==\"/>"
    "</tt:Parameters></tt:Rule>"
    "<tt:Rule Name=\"MyFieldDetector1\" Type=\"tt:FieldDetector\"><tt:Parameters>"
    "<tt:SimpleItem Name=\"Armed\" Value=\"true\"/></tt:Parameters></tt:Rule>"
    "</tt:RuleEngineConfiguration>"
    "</trt:Configurations></trt:GetVideoAnalyticsConfigurationsResponse></SOAP-ENV:Body></SOAP-ENV:Envelope>";

// The Axis's: a MotionRegionDetector polygon, no cell rule.
static const char* AXIS_ANALYTICS =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:tt=\"http://www.onvif.org/ver10/schema\" "
    "xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\"><SOAP-ENV:Body><trt:GetVideoAnalyticsConfigurationsResponse>"
    "<trt:Configurations token=\"0\"><tt:Name>analytics0</tt:Name><tt:UseCount>0</tt:UseCount>"
    "<tt:AnalyticsEngineConfiguration/><tt:RuleEngineConfiguration>"
    "<tt:Rule Name=\"Profile 1\" Type=\"tt:MotionRegionDetector\"><Parameters xmlns=\"http://www.onvif.org/ver10/schema\">"
    "<tt:ElementItem Name=\"MotionRegion\"/></Parameters></tt:Rule>"
    "</tt:RuleEngineConfiguration></trt:Configurations>"
    "</trt:GetVideoAnalyticsConfigurationsResponse></SOAP-ENV:Body></SOAP-ENV:Envelope>";

void test_r_onvif_events::test_cell_motion_rules()
{
    auto rules = parse_cell_motion_rules(OFFICE_ANALYTICS);
    RTF_ASSERT_EQUAL(rules.size(), (size_t)1);
    RTF_ASSERT_EQUAL(rules[0].configuration_token, string("0001"));
    RTF_ASSERT_EQUAL(rules[0].rule_name, string("MyMotionDetector"));
    RTF_ASSERT_EQUAL(rules[0].columns, 22);
    RTF_ASSERT_EQUAL(rules[0].rows, 18);
    RTF_ASSERT_EQUAL(rules[0].active_cells, string("0P8A8A=="));

    RTF_ASSERT(parse_cell_motion_rules(AXIS_ANALYTICS).empty());

    bool threw = false;
    try { parse_cell_motion_rules("not xml"); }
    catch (const std::exception&) { threw = true; }
    RTF_ASSERT(threw);
}
