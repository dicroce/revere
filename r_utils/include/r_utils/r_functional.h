#ifndef _r_utils_r_functional_h
#define _r_utils_r_functional_h

#include "r_utils/r_macro.h"
#include <vector>
#include <algorithm>
#include <type_traits>
#include <numeric>
#include <functional>
#include <vector>
#include <iterator>

namespace r_utils
{

namespace r_funky
{

// funky provides some rudimentary functional programming facilities. In most
// cases these functions are simply providing value semantics on top of existing
// std::algorithm's.. Which of course comes at the cost of performance, but they
// are super useful if you're careful.

template<typename C, typename F>
R_API auto fmap(const C& cont, F f)
{
    std::vector<typename C::value_type> out(cont.size());
    std::transform(std::begin(cont), std::end(cont), std::begin(out), f);
    return out;
}

template<typename C, typename F>
R_API auto filter(const C& cont, F f)
{
    std::vector<typename C::value_type> out;
    std::copy_if(std::begin(cont), std::end(cont), std::back_inserter(out), f);
    return out;
}

template<typename C, typename F, typename INIT>
R_API auto reduce(const C& cont, INIT init, F f)
{
    return std::accumulate(std::begin(cont), std::end(cont), init, f);
}

// Returns the items in a that are not in b.
template<typename C>
R_API auto set_diff(const C& a, const C& b)
{
    std::vector<typename C::value_type> output;
    for(auto& s : a)
    {
        auto i = begin(b), e = end(b), f = end(b);
        for( ; i != e; ++i)
        {
            if(*i == s)
                f = i;
        }

        if(f == end(b))
            output.push_back(s);
    }

    return output;
}

// This is primarily useful for map<>'s since they have no similar functionality (until library fundamentals TS v2)
template<typename CONT, typename PRED>
R_API void erase_if(CONT& c, const PRED& p)
{
    for(auto it = c.begin(); it != c.end();)
    {
        if(p(*it))
            it = c.erase(it);
        else ++it;
    }
}

template <class InputIterator, class OutputIterator, class UnaryOperator, class Pred>
R_API OutputIterator transform_if(InputIterator first1, InputIterator last1,
                                  OutputIterator result, UnaryOperator op, Pred pred)
{
    while (first1 != last1) 
    {
        if (pred(*first1)) {
            *result = op(*first1);
            ++result;
        }
        ++first1;
    }
    return result;
}

}

}

#endif
