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
     *
     * @param input The input image to process
     * @param skip_stats_update If true, don't update the moving average or motion frequency map.
     *                          MOG2 background model still updates so it tracks the visual scene.
     *                          Useful for catchup/in-event processing where we want motion detection
     *                          results but don't want to adapt the baseline statistics.
     */
    R_API r_utils::r_nullable<r_motion_info> process(const r_image& input, bool skip_stats_update = false);

    /**
     * Feed a cv::Mat image directly and receive motion metrics for that frame.
     * This overload accepts ROI Mats (zero-copy subregions of larger images).
     * Returns empty nullable on the very first call (no background yet).
     *
     * @param input The input image (BGR, RGB, or grayscale). Can be an ROI of a larger image.
     * @param roi_offset_x Offset to add to motion bbox x coordinates (for letterbox correction)
     * @param roi_offset_y Offset to add to motion bbox y coordinates (for letterbox correction)
     * @param skip_stats_update If true, don't update the moving average or motion frequency map.
     */
    R_API r_utils::r_nullable<r_motion_info> process(const cv::Mat& input,
                                                      int roi_offset_x = 0,
                                                      int roi_offset_y = 0,
                                                      bool skip_stats_update = false);

private:
    // statistics
    r_utils::r_exp_avg<uint64_t> _avg_motion;

    // MOG2 background subtractor
    cv::Ptr<cv::BackgroundSubtractorMOG2> _mog2;

    // tuning parameters
    const double _illumChangeThresh = 0.25;   // % of pixels changed ⇒ treat as illumination event
    const double _minAreaFraction   = 0.003;   // 1% of frame (adjustable)

    // warmup tracking - skip first few frames while MOG2 builds background model
    const size_t _warmupThreshold {5};        // number of frames to skip at startup
    size_t _warmupFrames {0};                 // frames processed during warmup

    // baseline learning - always update baseline during first N frames to establish it
    const size_t _baselineLearningFrames {30}; // frames to learn baseline (unconditionally update stats)
    
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
    cv::Mat _fgMask;
    cv::Mat _morphKernel;
};

} // namespace r_motion

#endif /* __r_motion_r_motion_state_h__ */