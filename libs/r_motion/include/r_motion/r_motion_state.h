#ifndef __r_motion_r_motion_state_h__
#define __r_motion_r_motion_state_h__

#include "r_motion/utils.h"
#include "r_utils/r_avg.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include <opencv2/opencv.hpp>
#include <functional>

namespace r_motion
{

struct r_motion_info
{
    r_image motion_pixels;            // reserved for future pixel mask use
    uint64_t motion      {0};         // total moving‑pixel count this frame
    uint64_t avg_motion  {0};         // exponential moving average
    uint64_t stddev      {0};         // std‑dev on the same EMA window
    
    // motion masking information
    uint64_t motion_before_mask {0};  // motion count before applying static mask
    uint64_t masked_pixels {0};       // number of pixels suppressed by static mask
    bool masking_active {false};      // whether motion masking is active
    
    // motion spatial information
    struct motion_region {
        int x, y, width, height;      // bounding box of all motion areas
        bool has_motion {false};      // whether any significant motion was detected
    } motion_bbox;
};

class r_motion_state
{
public:
    explicit R_API r_motion_state(size_t memory = 500, 
                                  double motionFreqThresh = 0.95,
                                  // higher = more weight on history, slower to react. lower = less weight on history, faster to react
                                  double freqDecayRate = 0.70,
                                  size_t minObservationFrames = 100,
                                  bool enableMasking = true);
    R_API r_motion_state(const r_motion_state&)            = delete;
    R_API r_motion_state(r_motion_state&&)                 = delete;
    R_API ~r_motion_state() noexcept;

    R_API r_motion_state& operator=(const r_motion_state&)  = delete;
    R_API r_motion_state& operator=(r_motion_state&&)       = delete;

    /**
     * Feed a BGRA or BGR image and receive motion metrics for that frame.
     * Returns empty nullable on the very first call (no background yet).
     */
    R_API r_utils::r_nullable<r_motion_info> process(const r_image& input);

private:
    // statistics
    r_utils::r_exp_avg<uint64_t> _avg_motion;

    // background model (CV_32F running average)
    cv::Mat _bgFloat;
    bool    _bgInit {false};

    // tuning parameters
    const double _learningRate      = 0.002;  // slow, stable
    const double _fastLearnRate     = 0.10;   // one‑shot to absorb lighting jump
    const double _adaptiveK         = 2.0;    // mean + k·σ threshold
    const double _illumChangeThresh = 0.25;   // % of pixels changed ⇒ treat as illumination event
    const double _minAreaFraction   = 0.003;   // 1% of frame (adjustable)
    
    // motion frequency masking parameters
    const double _motionFreqThresh;           // threshold for marking pixels as continuously moving
    const double _freqDecayRate;              // decay rate for motion frequency map
    const size_t _minObservationFrames;       // minimum frames before applying masking
    const bool _enableMasking;                // whether to enable masking at all

    // motion frequency tracking
    cv::Mat _motionFreqMap;                   // per-pixel motion frequency counter (CV_32F)
    cv::Mat _staticMask;                      // binary mask for suppressing dynamic regions
    size_t _frameCount {0};                   // total frames processed
    
    // reusable buffers to avoid reallocs
    cv::Mat _currGray;
    cv::Mat _blurred;
    cv::Mat _diff;
    cv::Mat _thresh;
    cv::Mat _morphKernel;
};

} // namespace r_motion

#endif /* __r_motion_r_motion_state_h__ */