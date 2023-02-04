
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
    R_API r_exp_avg(const r_exp_avg&) = default;
    R_API r_exp_avg(r_exp_avg&&) = default;
    R_API r_exp_avg(const T& init_value, T n) :
                    _accumulator(init_value),
                    _variance_accumulator(0),
                    _update_count(0),
                    _alpha(2./(n+1.))
    {
    }
    R_API ~r_exp_avg() noexcept = default;
    R_API r_exp_avg& operator=(const r_exp_avg&) = default;
    R_API r_exp_avg& operator=(r_exp_avg&&) = default;

    R_API T update(T new_val)
    {
        _accumulator = (T)((_alpha * new_val) + (1.0 - _alpha) * _accumulator);
        auto delta = new_val - _accumulator;
        _variance_accumulator += (delta * delta);
        ++_update_count;
        return _accumulator;
    }

    R_API T variance() const
    {
        return _variance_accumulator / _update_count;
    }
    
    R_API T standard_deviation() const
    {
        return (T)std::sqrt(variance());
    }

private:
    T _accumulator;
    T _variance_accumulator;
    uint64_t _update_count;
    double _alpha;
};

}

#endif
