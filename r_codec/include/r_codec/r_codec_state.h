#ifndef r_codec_r_codec_state_h
#define r_codec_r_codec_state_h

namespace r_codec
{

enum r_codec_state
{
    R_CODEC_STATE_INITIALIZED,
    R_CODEC_STATE_HUNGRY,
    R_CODEC_STATE_AGAIN,
    R_CODEC_STATE_AGAIN_HAS_OUTPUT,
    R_CODEC_STATE_HAS_OUTPUT,
    R_CODEC_STATE_EOF
};

}

#endif
