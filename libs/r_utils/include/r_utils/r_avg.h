
#ifndef __r_utils_r_avg_h
#define __r_utils_r_avg_h

#include "r_utils/r_macro.h"
#include <cstdlib>
#include <cstdint>
#include <cmath>
namespace r_utils
{

template<typename T>
class r_exp_avg final
{
public:
    R_API r_exp_avg()
      : _accumulator(T{})
      , _variance_accumulator(T{})
      , _alpha(1.0)
      , _initialized(false)
    {}

    R_API r_exp_avg(const r_exp_avg&) = default;
    R_API r_exp_avg(r_exp_avg&&) = default;

    // “period” constructor: α = 2/(n+1)
    R_API r_exp_avg(const T& init_value, T n)
      : _accumulator(init_value)
      , _variance_accumulator(init_value * init_value)
      , _alpha(2.0/(n + 1.0))
      , _initialized(true)
    {}

    R_API ~r_exp_avg() noexcept = default;
    R_API r_exp_avg& operator=(const r_exp_avg&) = default;
    R_API r_exp_avg& operator=(r_exp_avg&&) = default;

    // Feed in a new sample, return updated EMA
    R_API T update(T new_val)
    {
        if (!_initialized)
        {
            // first sample: seed both EMA and EMA[x^2]
            _accumulator = new_val;
            _variance_accumulator = new_val * new_val;
            _initialized = true;
        }
        else
        {
            _accumulator = static_cast<T>(
                (_alpha * new_val) +
                ((1.0 - _alpha) * _accumulator)
            );
            _variance_accumulator = static_cast<T>(
                (_alpha * (new_val * new_val)) +
                ((1.0 - _alpha) * _variance_accumulator)
            );
        }
        return _accumulator;
    }

    // Get current EMA value without updating
    R_API T value() const
    {
        return _accumulator;
    }

    // Exponential moving variance = E[x^2] - (E[x])^2
    R_API T variance() const
    {
        T var = _variance_accumulator - (_accumulator * _accumulator);
        // guard tiny negatives due to floating-point
        return var > T{} ? var : T{};
    }
    
    R_API T standard_deviation() const
    {
        return static_cast<T>(std::sqrt(variance()));
    }

private:
    T      _accumulator;            // EMA of x
    T      _variance_accumulator;   // EMA of x^2
    double _alpha;                  // smoothing factor
    bool   _initialized;            // first-sample flag
};

}

#endif
