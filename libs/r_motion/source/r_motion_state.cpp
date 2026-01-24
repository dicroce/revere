#include "r_motion/r_motion_state.h"
#include "r_utils/r_logger.h"

using namespace r_motion;
using namespace r_utils;
using cv::Size;

r_motion_state::r_motion_state(size_t memory, 
                               double motionFreqThresh,
                               double freqDecayRate,
                               size_t minObservationFrames,
                               bool enableMasking)
: _avg_motion(0, memory)
, _mog2(cv::createBackgroundSubtractorMOG2(500, 16, true))
, _motionFreqThresh(motionFreqThresh)
, _freqDecayRate(freqDecayRate)
, _minObservationFrames(minObservationFrames)
, _enableMasking(enableMasking)
, _morphKernel(cv::getStructuringElement(cv::MORPH_RECT, Size(3,3)))
{
}

r_motion_state::~r_motion_state() noexcept = default;

r_nullable<r_motion_info> r_motion_state::process(const r_image& input, bool skip_stats_update)
{
    if(input.width == 0 || input.height == 0)
        return r_nullable<r_motion_info>();  // empty frame guard

    cv::Mat src;

    // Handle different input formats - wrap incoming buffer (no copy)
    if(input.type == R_MOTION_IMAGE_TYPE_ARGB)
    {
        src = cv::Mat(input.height, input.width, CV_8UC4,
                      const_cast<unsigned char*>(input.data.data()));
    }
    else if(input.type == R_MOTION_IMAGE_TYPE_BGR)
    {
        src = cv::Mat(input.height, input.width, CV_8UC3,
                      const_cast<unsigned char*>(input.data.data()));
    }
    else if(input.type == R_MOTION_IMAGE_TYPE_RGB)
    {
        src = cv::Mat(input.height, input.width, CV_8UC3,
                      const_cast<unsigned char*>(input.data.data()));
    }
    else
    {
        return r_nullable<r_motion_info>(); // Unsupported format
    }

    // Delegate to cv::Mat overload with no offset
    return process(src, 0, 0, skip_stats_update);
}

r_nullable<r_motion_info> r_motion_state::process(const cv::Mat& input, int roi_offset_x, int roi_offset_y, bool skip_stats_update)
{
    r_nullable<r_motion_info> result;

    if(input.empty())
        return result;

    // --- GRAYSCALE + BLUR -----------------------------------------------------------
    // Detect input format and convert to grayscale
    if(input.channels() == 4)
    {
        cv::cvtColor(input, _currGray, cv::COLOR_BGRA2GRAY);
    }
    else if(input.channels() == 3)
    {
        // Assume RGB (caller from motion engine sends RGB)
        cv::cvtColor(input, _currGray, cv::COLOR_RGB2GRAY);
    }
    else if(input.channels() == 1)
    {
        _currGray = input; // Already grayscale (may be ROI, no copy)
    }
    else
    {
        return result; // Unsupported format
    }
    cv::GaussianBlur(_currGray, _blurred, Size(5,5), 0);

    // --- MOG2 BACKGROUND SUBTRACTION -----------------------------------------------
    // apply() automatically updates the background model
    // learningRate of -1 means "auto" (usually 1/history)
    // Always let MOG2 update so it tracks the visual scene properly
    _mog2->apply(_blurred, _fgMask, -1);

    // --- WARMUP PERIOD -------------------------------------------------------------
    // Skip the first few frames while MOG2 builds its background model.
    // Without a background, MOG2 reports everything as "motion" on the first frame,
    // which would pollute our stddev and make real motion detection impossible.
    _warmupFrames++;
    if(_warmupFrames <= _warmupThreshold)
    {
        R_LOG_INFO("MOTION: Warmup frame %zu/%zu - skipping stats", _warmupFrames, _warmupThreshold);
        return result;  // return empty until background model is stable
    }

    // --- SHADOW REMOVAL -----------------------------------------------------------
    // MOG2 marks shadows as 127 (gray). We only want actual motion (255).
    // Threshold at 250 to keep only white pixels.
    cv::threshold(_fgMask, _fgMask, 250, 255, cv::THRESH_BINARY);

    // --- ILLUMINATION CHANGE VETO ---------------------------------------------------
    // If a huge portion of the screen changed, it's likely a light switch or camera gain adjustment.
    // We veto this frame to avoid a massive false positive event.
    const double changeRatio = cv::countNonZero(_fgMask) / static_cast<double>(_fgMask.total());
    if(changeRatio > _illumChangeThresh)
    {
        R_LOG_INFO("MOTION: Illumination veto triggered - changeRatio=%.3f threshold=%.3f", changeRatio, _illumChangeThresh);
        return result;  // nothing emitted
    }

    // --- MORPHOLOGICAL CLEANUP (closing) -------------------------------------------
    cv::dilate(_fgMask, _fgMask, _morphKernel, cv::Point(-1,-1), 1);
    cv::erode (_fgMask, _fgMask, _morphKernel, cv::Point(-1,-1), 1);

    // --- MOTION FREQUENCY MAP UPDATE -----------------------------------------------
    // Only update frame count and frequency map if not in skip_stats_update mode
    if(!skip_stats_update)
        _frameCount++;

    // Track motion before masking for statistics
    uint64_t motion_before_mask = cv::countNonZero(_fgMask);

    // Initialize frequency map if needed (always do this even in skip_update mode)
    if(_motionFreqMap.empty() || _motionFreqMap.size() != _fgMask.size())
    {
        _motionFreqMap = cv::Mat::zeros(_fgMask.size(), CV_32F);
        _staticMask = cv::Mat::ones(_fgMask.size(), CV_8U);
    }

    // Update motion frequency map using exponential moving average to keep values in range (0-1)
    // Only update if not in skip_stats_update mode
    if(!skip_stats_update)
    {
        cv::Mat motionNormalized;
        _fgMask.convertTo(motionNormalized, CV_32F, 1.0/255.0);

        // Exponential moving average: freq = freq * decay + motion * (1 - decay)
        _motionFreqMap = _motionFreqMap * _freqDecayRate + motionNormalized * (1.0 - _freqDecayRate);
    }
    
    // Generate static mask if we have enough observations and masking is enabled
    bool masking_active = (_enableMasking && _frameCount >= _minObservationFrames);
    uint64_t masked_pixels = 0;
    
    if(masking_active)
    {
        cv::threshold(_motionFreqMap, _staticMask, _motionFreqThresh, 255, cv::THRESH_BINARY_INV);
        _staticMask.convertTo(_staticMask, CV_8U);
        
        // Calculate how many pixels will be masked
        cv::Mat maskedMotion;
        cv::bitwise_and(_fgMask, _staticMask, maskedMotion);
        masked_pixels = motion_before_mask - cv::countNonZero(maskedMotion);
        
        // Apply static mask to suppress motion in continuously moving areas
        _fgMask = maskedMotion;
    }

    // --- CONTOUR FILTERING ----------------------------------------------------------
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(_fgMask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    const double minAreaPx = _minAreaFraction * _fgMask.total();
    uint64_t motion_pixels = 0;
    
    // Calculate motion bounding box
    std::vector<cv::Rect> motion_rects;
    for(const auto& c : contours)
    {
        double area = cv::contourArea(c);
        if(area >= minAreaPx) {
            motion_pixels += static_cast<uint64_t>(area);
            motion_rects.push_back(cv::boundingRect(c));
        }
    }

    // --- METRICS OUTPUT -------------------------------------------------------------
    r_motion_info mi;
    mi.motion               = motion_pixels;

    // Get current baseline values first (before any potential update)
    mi.avg_motion           = _avg_motion.value();
    mi.stddev               = _avg_motion.standard_deviation();

    // Baseline learning: During the first N frames after warmup, we need to establish
    // a baseline. Without this, avg=0 and stddev=0, so ANY motion > 0 is considered
    // significant, and the baseline never gets updated (chicken-and-egg problem).
    // During learning, we use a simple threshold to decide if motion is "low enough"
    // to be considered baseline noise. We only learn from low-motion frames to avoid
    // polluting the baseline if someone walks in front during startup.
    bool in_learning_phase = (_frameCount < _baselineLearningFrames);

    // Only update the moving average if:
    // 1. Not in skip_stats_update mode (catchup processing)
    // 2. Either (in learning phase AND motion is low) OR motion is NOT significant
    // This prevents motion events from inflating the stddev and making subsequent
    // frames of the same event not register as significant (after learning).
    if(!skip_stats_update)
    {
        bool is_significant = is_motion_significant(mi.motion, mi.avg_motion, mi.stddev);

        // During learning, use a fixed threshold to filter out high-motion frames
        // 1000 pixels is a reasonable threshold for "baseline noise" vs "real motion"
        bool is_low_motion_for_learning = (mi.motion < 1000);

        if((in_learning_phase && is_low_motion_for_learning) || !is_significant)
        {
            _avg_motion.update(mi.motion);
            // Update returned values to reflect the new baseline
            mi.avg_motion   = _avg_motion.value();
            mi.stddev       = _avg_motion.standard_deviation();
        }
    }
    mi.motion_before_mask   = motion_before_mask;
    mi.masked_pixels        = masked_pixels;
    mi.masking_active       = masking_active;
    
    // Calculate overall motion bounding box
    if (!motion_rects.empty()) {
        cv::Rect combined_bbox = motion_rects[0];
        for (size_t i = 1; i < motion_rects.size(); i++) {
            combined_bbox |= motion_rects[i];  // Union of rectangles
        }
        // Apply ROI offset to transform from ROI-local coords to full image coords
        mi.motion_bbox.x = combined_bbox.x + roi_offset_x;
        mi.motion_bbox.y = combined_bbox.y + roi_offset_y;
        mi.motion_bbox.width = combined_bbox.width;
        mi.motion_bbox.height = combined_bbox.height;
        mi.motion_bbox.has_motion = true;
    } else {
        mi.motion_bbox.x = 0;
        mi.motion_bbox.y = 0;
        mi.motion_bbox.width = 0;
        mi.motion_bbox.height = 0;
        mi.motion_bbox.has_motion = false;
    }

    result.set_value(mi);
    return result;
}
