#ifndef r_utils_r_pool_h
#define r_utils_r_pool_h

#include <list>
#include <mutex>
#include <memory>
#include "r_utils/r_exception.h"

class test_r_utils_r_pool;

namespace r_utils
{

// r_pool is a simple, thread safe object pool. r_pool is templated on the object type, and
// constructs all of the instances it owns in it's constructor.
//
// r_pool::get() returns a special shared_ptr that will automagically put the object back in the
// pool when it (and all copies of it) go away.
//
// Because shared_ptr<>'s tend to be passed around, this object is also thread safe (the methods
// (other than the ctor & dtor) that modify the pool grab a lock).

template<class T>
class r_pool final
{
    friend class ::test_r_utils_r_pool;
public:
    template<typename... Args>
    R_API r_pool(size_t num, Args&&... args)
    {
        // create num number of T's with whatever constructor arguments are provided via args.
        for( size_t i = 0; i < num; ++i )
        {
            std::shared_ptr<T> t = std::make_shared<T>( std::forward<Args>(args)... );
            _all.push_back( t );
            _free.push_back( t.get() );
        }
    }

    R_API r_pool(const r_pool&) = delete;
    R_API r_pool(r_pool&&) = delete;

    R_API r_pool& operator=(const r_pool&) = delete;
    R_API r_pool& operator=(r_pool&&) = delete;

    R_API std::shared_ptr<T> get()
    {
        std::unique_lock<std::recursive_mutex> g( _lok );

        if( _free.empty() )
            R_STHROW(r_internal_exception, ("r_pool out of buffers."));

        // Construct a shared_ptr with a custom deleter lambda that puts the pointer back in _free.
        std::shared_ptr<T> f( _free.front(), [this](T* f) {
            std::unique_lock<std::recursive_mutex> g2( _lok );
            _free.push_back( f );
        } );

        _free.pop_front();

        return f;
    }

    R_API bool empty() const { std::unique_lock<std::recursive_mutex> g( _lok ); return _free.empty(); }

    R_API size_t get_num_free() const { std::unique_lock<std::recursive_mutex> g( _lok ); return _free.size(); }

private:
    std::list<T*> _free;
    std::list<std::shared_ptr<T> > _all;
    mutable std::recursive_mutex _lok;
};

}

#endif
