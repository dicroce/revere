
#include "framework.h"

// Offline tests of the GetProfiles parse and the substream pick: no camera or network needed.
class test_r_onvif_profiles : public test_fixture
{
public:
    RTF_FIXTURE(test_r_onvif_profiles);
      TEST(test_r_onvif_profiles::test_profile_fields);
      TEST(test_r_onvif_profiles::test_other_prefixes_and_missing_fields);
      TEST(test_r_onvif_profiles::test_unparseable_response_throws);
      TEST(test_r_onvif_profiles::test_profile_token_bounds);
      TEST(test_r_onvif_profiles::test_source_token_bounds);
      TEST(test_r_onvif_profiles::test_enum_bounds);
      TEST(test_r_onvif_profiles::test_substream_is_the_same_sensor);
      TEST(test_r_onvif_profiles::test_substream_codec_and_size);
      TEST(test_r_onvif_profiles::test_substream_rank);
      TEST(test_r_onvif_profiles::test_no_substream);
      TEST(test_r_onvif_profiles::test_ptz_caps);
    RTF_FIXTURE_END();

    virtual ~test_r_onvif_profiles() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_profile_fields();
    void test_other_prefixes_and_missing_fields();
    void test_unparseable_response_throws();
    void test_profile_token_bounds();
    void test_source_token_bounds();
    void test_enum_bounds();
    void test_substream_is_the_same_sensor();
    void test_substream_codec_and_size();
    void test_substream_rank();
    void test_no_substream();
    void test_ptz_caps();
};
