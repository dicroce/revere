#ifndef r_utils_r_nullable_h
#define r_utils_r_nullable_h

#include "r_utils/r_exception.h"
#include <type_traits>

namespace r_utils
{

/// Allows for a nullable value on the stack.
template<typename T>
class r_nullable
{
public:
    R_API r_nullable() :
        _value(),
        _is_null( true )
    {
    }

    R_API r_nullable(const r_nullable& obj) :
        _value(obj._value),
        _is_null(obj._is_null)
    {
    }

    R_API r_nullable(r_nullable&& obj) noexcept :
        _value(std::move(obj._value)),
        _is_null(std::move(obj._is_null))
    {
        obj._value = T();
        obj._is_null = true;
    }

    // Move constructor for T - preferred for rvalues
    R_API r_nullable(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>) :
        _value(std::move(value)),
        _is_null(false)
    {
    }

    // Copy constructor for T - for lvalues
    R_API r_nullable(const T& value) :
        _value(value),
        _is_null(false)
    {
    }

    R_API ~r_nullable() noexcept {}

    R_API r_nullable& operator = ( const r_nullable& rhs )
    {
        this->_value = rhs._value;
        this->_is_null = rhs._is_null;
        return *this;
    }

    R_API r_nullable& operator = (r_nullable&& rhs) noexcept
    {
        _value = std::move(rhs._value);
        _is_null = std::move(rhs._is_null);
        rhs._value = T();
        rhs._is_null = true;
        return *this;
    }

    // Move assignment from T
    R_API r_nullable& operator = (T&& rhs) noexcept(std::is_nothrow_move_assignable_v<T>)
    {
        this->_value = std::move(rhs);
        this->_is_null = false;
        return *this;
    }

    // Copy assignment from T
    R_API r_nullable& operator = (const T& rhs)
    {
        this->_value = rhs;
        this->_is_null = false;
        return *this;
    }

    R_API operator bool() const
    {
        return !_is_null;
    }

    R_API T& raw()
    {
        return _value;
    }

    R_API const T& value() const
    {
        return _value;
    }

    R_API void set_value( T value )
    {
        _value = value;
        _is_null = false;
    }

    R_API T take()
    {
        _is_null = true;
        return std::move(_value);
    }

    R_API void assign( T&& value )
    {
        _value = std::move(value);
        _is_null = false;
    }

    R_API bool is_null() const
    {
        return _is_null;
    }

    R_API void clear()
    {
        _value = T();
        _is_null = true;
    }

    R_API friend bool operator == ( const r_nullable& lhs, const r_nullable& rhs )
    {
        return( lhs._is_null && rhs._is_null ) || (lhs._value == rhs._value);
    }

    R_API friend bool operator == ( const r_nullable& lhs, const T& rhs )
    {
        return !lhs._is_null && lhs._value == rhs;
    }

    R_API friend bool operator == ( const T& lhs, const r_nullable& rhs )
    {
        return rhs == lhs;
    }

    R_API friend bool operator != ( const r_nullable& lhs, const r_nullable& rhs )
    {
        return !(lhs == rhs);
    }

    R_API friend bool operator != ( const r_nullable& lhs, const T& rhs )
    {
        return !(lhs == rhs);
    }

    R_API friend bool operator != ( const T& lhs, const r_nullable& rhs )
    {
        return !(lhs == rhs);
    }

    R_API T* operator->()
    {
        if (_is_null)
            R_THROW(("Attempting to access null r_nullable"));
        return &_value;
    }

    R_API const T* operator->() const
    {
        if (_is_null)
            R_THROW(("Attempting to access null r_nullable"));
        return &_value;
    }

private:
    T _value;
    bool _is_null;
};

template<typename T>
R_API bool operator == ( const T& lhs, const r_nullable<T>& rhs )
{
    return rhs == lhs;
}

template<typename T>
R_API bool operator != ( const T& lhs, const r_nullable<T>& rhs )
{
    return rhs != lhs;
}

}

#endif