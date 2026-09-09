#include "r_motion/r_motion_state.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace r_motion;
using namespace r_utils;
using cv::Size;

namespace
{

constexpr double SHADOW_WEIGHT = 0.35;
constexpr double ILLUMINATION_WEIGHT = 0.05;
constexpr double PERSISTENT_STRONG_WEIGHT = 0.25;
constexpr double PERSISTENT_SHADOW_WEIGHT = 0.10;
constexpr double PERSISTENT_ILLUMINATION_WEIGHT = 0.02;
constexpr int ILLUMINATION_MIN_SHIFT = 8;
constexpr int ILLUMINATION_TOLERANCE = 12;
constexpr double ILLUMINATION_EXPLAINED_FRACTION = 0.65;

struct illumination_result
{
    bool global_change {false};
    int median_delta {0};
};

illumination_result analyze_illumination(const cv::Mat& previous, const cv::Mat& current)
{
    illumination_result result;
    if(previous.empty() || previous.size() != current.size() ||
       previous.type() != CV_8UC1 || current.type() != CV_8UC1)
        return result;

    std::array<uint64_t, 511> histogram{};
    const uint64_t total = static_cast<uint64_t>(current.total());

    for(int y = 0; y < current.rows; ++y)
    {
        const auto* p = previous.ptr<uint8_t>(y);
        const auto* c = current.ptr<uint8_t>(y);
        for(int x = 0; x < current.cols; ++x)
            ++histogram[static_cast<size_t>(static_cast<int>(c[x]) - static_cast<int>(p[x]) + 255)];
    }

    uint64_t cumulative = 0;
    const uint64_t midpoint = total / 2;
    for(size_t i = 0; i < histogram.size(); ++i)
    {
        cumulative += histogram[i];
        if(cumulative >= midpoint)
        {
            result.median_delta = static_cast<int>(i) - 255;
            break;
        }
    }

    uint64_t explained = 0;
    const int lo = std::max(-255, result.median_delta - ILLUMINATION_TOLERANCE);
    const int hi = std::min(255, result.median_delta + ILLUMINATION_TOLERANCE);
    for(int delta = lo; delta <= hi; ++delta)
        explained += histogram[static_cast<size_t>(delta + 255)];

    result.global_change =
        std::abs(result.median_delta) >= ILLUMINATION_MIN_SHIFT &&
        explained >= static_cast<uint64_t>(ILLUMINATION_EXPLAINED_FRACTION * total);
    return result;
}

void retain_illumination_residual(cv::Mat& mask,
                                  const cv::Mat& previous,
                                  const cv::Mat& current,
                                  int median_delta)
{
    for(int y = 0; y < mask.rows; ++y)
    {
        auto* m = mask.ptr<uint8_t>(y);
        const auto* p = previous.ptr<uint8_t>(y);
        const auto* c = current.ptr<uint8_t>(y);
        for(int x = 0; x < mask.cols; ++x)
        {
            const int delta = static_cast<int>(c[x]) - static_cast<int>(p[x]);
            if(std::abs(delta - median_delta) <= ILLUMINATION_TOLERANCE)
                m[x] = 0;
        }
    }
}

double elapsed_scale(int64_t previous_ms, int64_t current_ms)
{
    if(previous_ms < 0 || current_ms < 0 || current_ms <= previous_ms)
        return 1.0;

    // Avoid snapping a model to a single frame after a long camera outage.
    return std::clamp((current_ms - previous_ms) / 1000.0, 0.001, 10.0);
}

uint64_t rounded_nonnegative(double value)
{
    if(value <= 0.0)
        return 0;
    return static_cast<uint64_t>(std::llround(value));
}

}

r_motion_state::r_motion_state(size_t memory,
                               double motionFreqThresh,
                               double freqDecayRate,
                               size_t minObservationFrames,
                               bool enableMasking,
                               double minAreaFraction)
: _statsMemory(std::max<size_t>(memory, 1))
, _mog2(cv::createBackgroundSubtractorMOG2(500, 16, true))
, _minAreaFraction(minAreaFraction)
, _motionFreqThresh(motionFreqThresh)
, _freqDecayRate(freqDecayRate)
, _minObservationFrames(minObservationFrames)
, _enableMasking(enableMasking)
, _morphKernel(cv::getStructuringElement(cv::MORPH_RECT, Size(3,3)))
{
}

r_motion_state::~r_motion_state() noexcept = default;

r_nullable<r_motion_info> r_motion_state::process(const r_image& input,
                                                   bool skip_stats_update,
                                                   int64_t timestamp_ms)
{
    r_nullable<r_motion_info> result;
    if(input.width == 0 || input.height == 0)
        return result;

    size_t channels = 0;
    switch(input.type)
    {
        case R_MOTION_IMAGE_TYPE_ARGB: channels = 4; break;
        case R_MOTION_IMAGE_TYPE_BGR:
        case R_MOTION_IMAGE_TYPE_RGB: channels = 3; break;
        case R_MOTION_IMAGE_TYPE_GRAY8: channels = 1; break;
        default: return result;
    }

    const size_t expected = static_cast<size_t>(input.width) * input.height * channels;
    if(input.data.size() < expected)
        return result;

    cv::Mat gray;
    if(input.type == R_MOTION_IMAGE_TYPE_ARGB)
    {
        cv::Mat argb(input.height, input.width, CV_8UC4,
                     const_cast<unsigned char*>(input.data.data()));
        cv::Mat rgba(input.height, input.width, CV_8UC4);
        const int from_to[] = {1, 0, 2, 1, 3, 2, 0, 3};
        cv::mixChannels(&argb, 1, &rgba, 1, from_to, 4);
        cv::cvtColor(rgba, gray, cv::COLOR_RGBA2GRAY);
    }
    else if(input.type == R_MOTION_IMAGE_TYPE_BGR)
    {
        cv::Mat bgr(input.height, input.width, CV_8UC3,
                    const_cast<unsigned char*>(input.data.data()));
        cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    }
    else if(input.type == R_MOTION_IMAGE_TYPE_RGB)
    {
        cv::Mat rgb(input.height, input.width, CV_8UC3,
                    const_cast<unsigned char*>(input.data.data()));
        cv::cvtColor(rgb, gray, cv::COLOR_RGB2GRAY);
    }
    else
    {
        gray = cv::Mat(input.height, input.width, CV_8UC1,
                       const_cast<unsigned char*>(input.data.data()));
    }

    return process(gray, 0, 0, skip_stats_update, timestamp_ms);
}

r_nullable<r_motion_info> r_motion_state::process(const cv::Mat& input,
                                                   int roi_offset_x,
                                                   int roi_offset_y,
                                                   bool skip_stats_update,
                                                   int64_t timestamp_ms)
{
    r_nullable<r_motion_info> result;
    if(input.empty())
        return result;

    if(input.channels() == 4)
        cv::cvtColor(input, _currGray, cv::COLOR_BGRA2GRAY);
    else if(input.channels() == 3)
        cv::cvtColor(input, _currGray, cv::COLOR_RGB2GRAY);
    else if(input.channels() == 1)
        _currGray = input;
    else
        return result;

    cv::GaussianBlur(_currGray, _blurred, Size(5,5), 0);

    double mog_learning_rate = -1.0;
    if(timestamp_ms >= 0 && _lastInputTimestampMs >= 0 && _warmupFrames >= _warmupThreshold)
    {
        const double scale = elapsed_scale(_lastInputTimestampMs, timestamp_ms);
        mog_learning_rate = 1.0 - std::pow(1.0 - (1.0 / 500.0), scale);
    }
    _mog2->apply(_blurred, _fgMask, mog_learning_rate);
    _lastInputTimestampMs = timestamp_ms;

    ++_warmupFrames;
    if(_warmupFrames <= _warmupThreshold)
    {
        _blurred.copyTo(_prevBlurred);
        return result;
    }

    cv::compare(_fgMask, 255, _strongMask, cv::CMP_EQ);
    cv::compare(_fgMask, 127, _shadowMask, cv::CMP_EQ);
    cv::bitwise_or(_strongMask, _shadowMask, _combinedMask);
    if(!_illuminationMask.empty() && _illuminationMask.size() != _combinedMask.size())
        _illuminationMask.release();

    const double change_ratio = cv::countNonZero(_combinedMask) /
                                static_cast<double>(_combinedMask.total());
    const auto illumination = analyze_illumination(_prevBlurred, _blurred);
    const bool illumination_change =
        change_ratio > _illumChangeThresh && illumination.global_change;

    if(illumination_change)
    {
        _combinedMask.copyTo(_illuminationMask);
        retain_illumination_residual(_strongMask, _prevBlurred, _blurred,
                                     illumination.median_delta);
        retain_illumination_residual(_shadowMask, _prevBlurred, _blurred,
                                     illumination.median_delta);
    }
    else if(_illuminationMask.empty() || change_ratio <= _illumChangeThresh)
        _illuminationMask = cv::Mat::zeros(_combinedMask.size(), CV_8U);
    _blurred.copyTo(_prevBlurred);

    cv::morphologyEx(_strongMask, _strongMask, cv::MORPH_CLOSE, _morphKernel);
    cv::morphologyEx(_shadowMask, _shadowMask, cv::MORPH_CLOSE, _morphKernel);
    cv::bitwise_or(_strongMask, _shadowMask, _combinedMask);
    if(illumination_change)
    {
        // Keep globally explained pixels as very weak evidence. One light
        // switch frame cannot satisfy event confirmation, while a large object
        // is no longer irreversibly erased at this layer.
        cv::Mat residual_inverse;
        cv::bitwise_not(_combinedMask, residual_inverse);
        cv::bitwise_and(_illuminationMask, residual_inverse, _illuminationMask);
    }
    if(!_illuminationMask.empty())
        cv::bitwise_or(_combinedMask, _illuminationMask, _combinedMask);

    if(!skip_stats_update)
    {
        ++_frameCount;
        if(timestamp_ms >= 0 && _firstObservationTimestampMs < 0)
            _firstObservationTimestampMs = timestamp_ms;
    }

    const uint64_t motion_before_mask = cv::countNonZero(_combinedMask);

    if(_motionFreqMap.empty() || _motionFreqMap.size() != _combinedMask.size())
    {
        _motionFreqMap = cv::Mat::zeros(_combinedMask.size(), CV_32F);
        _staticMask = cv::Mat(_combinedMask.size(), CV_8U, cv::Scalar(255));
        _lastFrequencyTimestampMs = -1;
        _firstObservationTimestampMs = timestamp_ms;
    }

    if(!skip_stats_update)
    {
        cv::Mat motion_normalized;
        _combinedMask.convertTo(motion_normalized, CV_32F, 1.0 / 255.0);
        const double scale = elapsed_scale(_lastFrequencyTimestampMs, timestamp_ms);
        const double decay = timestamp_ms >= 0
            ? std::pow(_freqDecayRate, scale)
            : _freqDecayRate;
        cv::addWeighted(_motionFreqMap, decay,
                        motion_normalized, 1.0 - decay, 0.0, _motionFreqMap);
        _lastFrequencyTimestampMs = timestamp_ms;
    }

    bool masking_active = false;
    if(_enableMasking)
    {
        if(timestamp_ms >= 0 && _firstObservationTimestampMs >= 0)
            masking_active = (timestamp_ms - _firstObservationTimestampMs) >=
                             static_cast<int64_t>(_minObservationFrames) * 1000;
        else
            masking_active = _frameCount >= _minObservationFrames;
    }

    uint64_t persistent_pixels = 0;
    if(masking_active)
    {
        cv::threshold(_motionFreqMap, _staticMask, _motionFreqThresh,
                      255, cv::THRESH_BINARY_INV);
        _staticMask.convertTo(_staticMask, CV_8U);
        cv::Mat persistent_mask;
        cv::bitwise_not(_staticMask, persistent_mask);
        cv::bitwise_and(_combinedMask, persistent_mask, persistent_mask);
        persistent_pixels = cv::countNonZero(persistent_mask);
    }

    cv::Mat labels;
    cv::Mat stats;
    cv::Mat centroids;
    const int label_count = cv::connectedComponentsWithStats(
        _combinedMask, labels, stats, centroids, 8, CV_32S);

    const double configured_min_area = _minAreaFraction * _combinedMask.total();
    const double direct_component_area = std::max(9.0, configured_min_area * 0.50);
    const double fragment_component_area = std::max(4.0, configured_min_area * 0.15);

    uint64_t fragment_area_total = 0;
    for(int label = 1; label < label_count; ++label)
    {
        const int area = stats.at<int>(label, cv::CC_STAT_AREA);
        if(area >= fragment_component_area)
            fragment_area_total += static_cast<uint64_t>(area);
    }
    const bool accept_fragment_group = fragment_area_total >= configured_min_area;

    std::vector<uint8_t> accepted(static_cast<size_t>(label_count), 0);
    r_motion_info mi;
    cv::Rect combined_bbox;
    bool have_bbox = false;

    for(int label = 1; label < label_count; ++label)
    {
        const int area = stats.at<int>(label, cv::CC_STAT_AREA);
        if(area < direct_component_area &&
           !(accept_fragment_group && area >= fragment_component_area))
            continue;

        accepted[static_cast<size_t>(label)] = 1;
        cv::Rect rect(stats.at<int>(label, cv::CC_STAT_LEFT),
                      stats.at<int>(label, cv::CC_STAT_TOP),
                      stats.at<int>(label, cv::CC_STAT_WIDTH),
                      stats.at<int>(label, cv::CC_STAT_HEIGHT));
        combined_bbox = have_bbox ? (combined_bbox | rect) : rect;
        have_bbox = true;

        r_motion_info::motion_region region;
        region.x = rect.x + roi_offset_x;
        region.y = rect.y + roi_offset_y;
        region.width = rect.width;
        region.height = rect.height;
        region.has_motion = true;
        mi.motion_regions.push_back(region);
    }

    double weighted_motion = 0.0;
    for(int y = 0; y < labels.rows; ++y)
    {
        const auto* label_row = labels.ptr<int>(y);
        const auto* strong_row = _strongMask.ptr<uint8_t>(y);
        const auto* shadow_row = _shadowMask.ptr<uint8_t>(y);
        const auto* illumination_row = _illuminationMask.ptr<uint8_t>(y);
        const auto* static_row = _staticMask.ptr<uint8_t>(y);
        for(int x = 0; x < labels.cols; ++x)
        {
            const int label = label_row[x];
            if(label <= 0 || !accepted[static_cast<size_t>(label)])
                continue;

            const bool persistent = masking_active && static_row[x] == 0;
            if(illumination_row[x] != 0)
            {
                ++mi.weak_motion;
                ++mi.illumination_motion;
                weighted_motion += persistent
                    ? PERSISTENT_ILLUMINATION_WEIGHT
                    : ILLUMINATION_WEIGHT;
            }
            else if(strong_row[x] != 0)
            {
                ++mi.strong_motion;
                weighted_motion += persistent ? PERSISTENT_STRONG_WEIGHT : 1.0;
            }
            else if(shadow_row[x] != 0)
            {
                ++mi.weak_motion;
                weighted_motion += persistent ? PERSISTENT_SHADOW_WEIGHT : SHADOW_WEIGHT;
            }
            if(persistent)
                ++mi.persistent_motion;
        }
    }

    mi.motion = rounded_nonnegative(weighted_motion);
    mi.motion_before_mask = motion_before_mask;
    mi.masked_pixels = persistent_pixels;
    mi.masking_active = masking_active;
    mi.illumination_change = illumination_change;

    const double variance = std::max(0.0, _secondMoment - (_avgMotion * _avgMotion));
    mi.avg_motion = rounded_nonnegative(_avgMotion);
    mi.stddev = rounded_nonnegative(std::sqrt(variance));
    mi.significant = is_motion_significant(mi.motion, mi.avg_motion, mi.stddev);

    if(!skip_stats_update)
    {
        const bool in_learning_phase = timestamp_ms >= 0 && _firstObservationTimestampMs >= 0
            ? (timestamp_ms - _firstObservationTimestampMs) <
              static_cast<int64_t>(_baselineLearningFrames) * 1000
            : _frameCount < _baselineLearningFrames;
        const double learning_ceiling = std::max(32.0, configured_min_area * 0.25);
        const bool low_learning_motion = mi.motion < learning_ceiling;

        if((in_learning_phase && low_learning_motion) || !mi.significant)
        {
            const double base_alpha = 2.0 / (static_cast<double>(_statsMemory) + 1.0);
            const double scale = elapsed_scale(_lastStatsTimestampMs, timestamp_ms);
            const double alpha = timestamp_ms >= 0
                ? 1.0 - std::pow(1.0 - base_alpha, scale)
                : base_alpha;
            const double sample = static_cast<double>(mi.motion);
            _avgMotion = alpha * sample + (1.0 - alpha) * _avgMotion;
            _secondMoment = alpha * sample * sample + (1.0 - alpha) * _secondMoment;

            const double updated_variance =
                std::max(0.0, _secondMoment - (_avgMotion * _avgMotion));
            mi.avg_motion = rounded_nonnegative(_avgMotion);
            mi.stddev = rounded_nonnegative(std::sqrt(updated_variance));
        }
        _lastStatsTimestampMs = timestamp_ms;
    }

    if(have_bbox)
    {
        mi.motion_bbox.x = combined_bbox.x + roi_offset_x;
        mi.motion_bbox.y = combined_bbox.y + roi_offset_y;
        mi.motion_bbox.width = combined_bbox.width;
        mi.motion_bbox.height = combined_bbox.height;
        mi.motion_bbox.has_motion = true;
    }

    result.set_value(mi);
    return result;
}
