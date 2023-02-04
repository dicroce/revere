
#ifndef r_utils_r_byte_ptr_h
#define r_utils_r_byte_ptr_h

#include "r_utils/r_exception.h"
#include "r_utils/r_string_utils.h"
#include <string.h>
#include <cstddef>

namespace r_utils
{

/// Tests whether pointer 'pos' is within the buffer bounds for 'len' bytes.
static bool _in_bounds(const uint8_t* start, size_t blen, const uint8_t* pos, size_t len)
{
    return (pos >= start) && ((pos + len) <= (start + blen));
}

/// casts the pointer to the specified pointer type.
/// @note Only works on pointer types (i.e. not concrete types like int, char, etc).
template<typename T>
struct _r_byte_ptr_rw_helper
{
    R_API static T cast(const uint8_t* start, const uint8_t* current, size_t length)
    {
        T v = 0; // Required so we can get the size of the value *pointed* to
        if (!_in_bounds(start, length, current, sizeof(*v)))
        {
            R_STHROW(r_exception, ("casting to the specified type could access out-of-bounds memory. Start (%p), End (%p), Ptr (%p), Size (%llu)",
				     start, start + length, current, sizeof(*v)));
        }
        return (T)current;
    }
};

/// Specializes on void*, b/c we can't do a bounds check on this type.
template<>
struct _r_byte_ptr_rw_helper<const void*>
{
    R_API static const void* cast(const uint8_t*, const uint8_t* current, size_t)
    {
        return (const void*)current;
    }
};

template<>
struct _r_byte_ptr_rw_helper<void*>
{
   R_API static void* cast(const uint8_t*, const uint8_t* current, size_t)
   {
        return (void*)current;
   }
};

/// r_byte_ptr_rw provides a simple way to interact with and parse buffer data. It
/// behaves similar to a raw byte pointer, except it throws an exception if
/// it is dereferenced passed the bounds of the buffer (to prevent buffer
/// overruns when parsing).
///
/// Sample usage:
///
/// r_byte_ptr_rw p(buffer, length);
/// uint8_t hdr = *p++;
///
/// uint32_t ts = p.read<uint32_t>(); // Doesn't advance pointer
/// ts = p.consume<uint32_t>();       // Advances pointer
///
/// uint32_t ssrc = *(uint32_t*)p;
/// p += 4;
///
/// *(int16_t*)p = -42;
/// *p.cast<int16_t*>() = -42;
///
/// uint16_t size = *(uint16_t)&p[37];
///
/// p.write<uint32_t>(42);
///
class r_byte_ptr_rw
{
public:
    /// Initializes pointer to 0
    R_API r_byte_ptr_rw() :
        _start(0),
        _current(0),
        _length(0)
    {
    }

    /// Copies the current state of the other pointer
    R_API r_byte_ptr_rw(const r_byte_ptr_rw& other) :
        _start(other._start),
        _current(other._current),
        _length(other._length)
    {
    }

    R_API r_byte_ptr_rw(r_byte_ptr_rw&& other ) noexcept :
        _start(std::move(other._start)),
        _current(std::move(other._current)),
        _length(std::move(other._length))
    {
        other._start = 0;
        other._current = 0;
        other._length = 0;
    }

    /// Initializes the pointer to point to the beginning of the specified buffer
    R_API r_byte_ptr_rw(uint8_t* data, size_t length) :
        _start(data),
        _current(data),
        _length(length)
    {
    }

    R_API ~r_byte_ptr_rw() noexcept
    {
    }

    R_API r_byte_ptr_rw& operator=(const r_byte_ptr_rw& other)
    {
        return set_ptr(other._start, other._length);
    }

    R_API r_byte_ptr_rw& operator=(r_byte_ptr_rw&& other) noexcept
    {
        _start = std::move(other._start);
        _current = std::move(other._current);
        _length = std::move(other._length);

        other._start = 0;
        other._current = 0;
        other._length = 0;

        return *this;
    }

    R_API static r_byte_ptr_rw append(std::vector<uint8_t>& vec, size_t space)
    {
        auto currentCap = vec.capacity();
        auto currentSize = vec.size();
        auto avail = (currentCap>currentSize)?currentCap-currentSize:currentSize-currentCap;
        if(avail < space)
        {
            auto newCap = currentCap * 2;
            if(newCap < space)
                newCap = space;
            vec.reserve(newCap);
        }
        vec.resize(currentSize + space);
        return r_byte_ptr_rw(&vec[currentSize], space);
    }

    /// Resets the pointer to point to the beginning of the specified buffer
    R_API r_byte_ptr_rw& set_ptr(uint8_t* data, size_t length)
    {
        _start = _current = data;
        _length = length;
        return *this;
    }

    /// Current pointer value.
    R_API uint8_t* get_ptr() const
    {
        return _current;
    }

    /// Returns the length of the buffer pointed to.
    R_API size_t length() const
    {
        return _length;
    }

    /// Indicates if the current pointer value points to a valid position
    /// within the buffer.
    R_API bool in_bounds() const
    {
        return _in_bounds(_start, _length, _current, 1);
    }

    /// The original pointer value set on the object (i.e. the beginning of the buffer).
    /// @note This value is not affected by operations on the pointer value.
    R_API uint8_t* original_ptr() const
    {
        return _start;
    }

    /// Adjusts the current pointer position to point to the specified position in the buffer.
    R_API r_byte_ptr_rw& seek(size_t offset)
    {
        if (!_in_bounds(_start, _length, _start + offset, 1))
	        R_STHROW(r_invalid_argument_exception, ("Attempt to seek outside the buffer bounds. Offset (%llu), length(%llu)", offset, _length));
        _current = _start + offset;
        return *this;
    }

    /// Returns the current offest (in bytes) from the beginning of the buffer
    /// to the current pointer position.
    R_API ptrdiff_t offset() const
    {
        return _current - _start;
    }

    /// Returns the number of bytes left that are available to read from the buffer.
    R_API ptrdiff_t remaining() const
    {
        return (_start + _length) - _current;
    }

    /// Returns a reference to the byte value at the current pointer position.
    R_API uint8_t& operator*()
    {
        if (!in_bounds())
            R_STHROW(r_internal_exception, ("Attempt to deference a pointer outside the buffer bounds."));
        return *_current;
    }

    /// Returns a reference to the byte value at the specified index in the buffer.
    R_API uint8_t& operator[](size_t index)
    {
        if (!_in_bounds(_start, _length, _start + index, 1))
            R_STHROW(r_internal_exception, ("Array index outside the buffer bounds."));
        return *_current;
    }

    /// Returns the specified data type value at the current pointer position.
    template<typename T>
    R_API T read() const
    {
        if (!_in_bounds(_start, _length, _current, sizeof(T)))
        {
            R_STHROW(r_internal_exception, ("Attempt to read outside the buffer bounds. Start (%p), End (%p), Ptr (%p), Size (%llu)",
				     _start, _start + _length, _current, sizeof(T)));
        }
        return *(T*)_current;
    }

    /// Returns the specified data type value at the current pointer position AND increments
    /// the pointer past the data read.
    template<typename T>
    R_API T consume()
    {
        T value = read<T>();
        _current += sizeof(T);
        return value;
    }

    /// writes the specified data value to the current position in the buffer and increments
    /// the pointer past the data written.
    template<typename T>
    R_API T write(T value)
    {
        if (!_in_bounds(_start, _length, _current, sizeof(T)))
        {
	        R_STHROW(r_internal_exception, ("Attempt to write outside the buffer bounds. Start (%p), End (%p), Ptr (%p), Size (%llu)",
			 	     _start, _start + _length, _current, sizeof(T)));
        }
        *(T*)_current = value;
        _current += sizeof(T);
        return value;
    }

    R_API void bulk_write( const uint8_t* src, uint32_t len )
    {
        if (!_in_bounds(_start, _length, _current, len))
        {
            R_STHROW(r_internal_exception, ("Attempt to write outside the buffer bounds. Start (%p), End (%p), Ptr (%p), Size (%llu)",
				     _start, _start + _length, _current, len));
        }

        memcpy( _current, src, len );
        _current += len;
    }

    R_API void bulk_read( uint8_t* dst, uint32_t len )
    {
        if (!_in_bounds(_start, _length, _current, len))
        {
            R_STHROW(r_internal_exception, ("Attempt to read outside the buffer bounds. Start (%p), End (%p), Ptr (%p), Size (%llu)",
				     _start, _start + _length, _current, len));
        }

        memcpy( dst, _current, len );
        _current += len;
    }

    R_API uint8_t* operator++()
    {
        return ++_current;
    }

    R_API uint8_t* operator++(int)
    {
        return _current++;
    }

    R_API uint8_t* operator--()
    {
        return --_current;
    }

    R_API uint8_t* operator--(int)
    {
        return _current--;
    }

    R_API uint8_t* operator+(size_t step) const
    {
        return _current + step;
    }

    R_API uint8_t* operator-(size_t step) const
    {
        return _current - step;
    }

    R_API uint8_t* operator+=(size_t step)
    {
        return _current += step;
    }

    R_API uint8_t* operator-=(size_t step)
    {
        return _current -= step;
    }

    R_API bool operator==(const r_byte_ptr_rw& other) const
    {
        return other._current == _current;
    }

    R_API bool operator!=(const r_byte_ptr_rw& other) const
    {
        return other._current != _current;
    }

    R_API bool operator<=(const r_byte_ptr_rw& other) const
    {
        return _current <= other._current;
    }

    R_API bool operator>=(const r_byte_ptr_rw& other) const
    {
        return _current >= other._current;
    }

    R_API bool operator<(const r_byte_ptr_rw& other) const
    {
        return _current < other._current;
    }

    R_API bool operator>(const r_byte_ptr_rw& other) const
    {
        return _current > other._current;
    }

    /// Allows pointer to be cast to other pointer types only (not concrete types).
    template<typename to>
    R_API operator to() const
    {
        return _r_byte_ptr_rw_helper<to>::cast(_start, _current, _length);
    }

private:

    ///< The pointer to the start of the buffer.
    uint8_t* _start;

    ///< The current pointer value.
    uint8_t* _current;

    ///< The length of the buffer pointed to by '_start'
    size_t _length;
};

/// casts the pointer to the specified pointer type.
/// @note Only works on pointer types (i.e. not concrete types like int, char, etc).
template<typename T>
struct _r_byte_ptr_ro_helper
{
    static T cast(const uint8_t* start, const uint8_t* current, size_t length)
    {
        T v = 0; // Required so we can get the size of the value *pointed* to
        if (!_in_bounds(start, length, current, sizeof(*v)))
        {
            R_STHROW(r_exception, ("casting to the specified type could access out-of-bounds memory. Start (%p), End (%p), Ptr (%p), Size (%llu)",
				     start, start + length, current, sizeof(*v)));
        }
        return (T)current;
    }
};

/// Specializes on void*, b/c we can't do a bounds check on this type.
template<>
struct _r_byte_ptr_ro_helper<const void*>
{
    R_API static const void* cast(const uint8_t*, const uint8_t* current, size_t)
    {
        return (const void*)current;
    }
};

template<>
struct _r_byte_ptr_ro_helper<void*>
{
   R_API static void* cast(const uint8_t*, const uint8_t* current, size_t)
   {
        return (void*)current;
   }
};

class r_byte_ptr_ro
{
public:
    /// Initializes pointer to 0
    R_API r_byte_ptr_ro() :
        _start(0),
        _current(0),
        _length(0)
    {
    }

    /// Copies the current state of the other pointer
    R_API r_byte_ptr_ro(const r_byte_ptr_ro& other) :
        _start(other._start),
        _current(other._current),
        _length(other._length)
    {
    }

    R_API r_byte_ptr_ro(r_byte_ptr_ro&& other ) noexcept :
        _start(std::move(other._start)),
        _current(std::move(other._current)),
        _length(std::move(other._length))
    {
        other._start = 0;
        other._current = 0;
        other._length = 0;
    }

    /// Initializes the pointer to point to the beginning of the specified buffer
    R_API r_byte_ptr_ro(const uint8_t* data, size_t length) :
        _start(data),
        _current(data),
        _length(length)
    {
    }

    R_API r_byte_ptr_ro(const std::pair<size_t, const uint8_t*>& region) :
        _start(region.second),
        _current(region.second),
        _length(region.first)
    {
    }

    R_API ~r_byte_ptr_ro() noexcept
    {
    }

    R_API r_byte_ptr_ro& operator=(const r_byte_ptr_ro& other)
    {
        return set_ptr(other._start, other._length);
    }

    R_API r_byte_ptr_ro& operator=(r_byte_ptr_ro&& other) noexcept
    {
        _start = std::move(other._start);
        _current = std::move(other._current);
        _length = std::move(other._length);

        other._start = 0;
        other._current = 0;
        other._length = 0;

        return *this;
    }

    /// Resets the pointer to point to the beginning of the specified buffer
    R_API r_byte_ptr_ro& set_ptr(const uint8_t* data, size_t length)
    {
        _start = _current = data;
        _length = length;
        return *this;
    }

    /// Current pointer value.
    R_API const uint8_t* get_ptr() const
    {
        return _current;
    }

    /// Returns the length of the buffer pointed to.
    R_API size_t length() const
    {
        return _length;
    }

    /// Indicates if the current pointer value points to a valid position
    /// within the buffer.
    R_API bool in_bounds() const
    {
        return _in_bounds(_start, _length, _current, 1);
    }

    /// The original pointer value set on the object (i.e. the beginning of the buffer).
    /// @note This value is not affected by operations on the pointer value.
    R_API const uint8_t* original_ptr() const
    {
        return _start;
    }

    /// Adjusts the current pointer position to point to the specified position in the buffer.
    R_API r_byte_ptr_ro& seek(size_t offset)
    {
        if (!_in_bounds(_start, _length, _start + offset, 1))
	        R_STHROW(r_invalid_argument_exception, ("Attempt to seek outside the buffer bounds. Offset (%llu), length(%llu)", offset, _length));
        _current = _start + offset;
        return *this;
    }

    /// Returns the current offest (in bytes) from the beginning of the buffer
    /// to the current pointer position.
    R_API ptrdiff_t offset() const
    {
        return _current - _start;
    }

    /// Returns the number of bytes left that are available to read from the buffer.
    R_API ptrdiff_t remaining() const
    {
        return (_start + _length) - _current;
    }

    /// Returns a reference to the byte value at the current pointer position.
    R_API const uint8_t& operator*()
    {
        if (!in_bounds())
            R_STHROW(r_internal_exception, ("Attempt to deference a pointer outside the buffer bounds."));
        return *_current;
    }

    /// Returns a reference to the byte value at the specified index in the buffer.
    R_API const uint8_t& operator[](size_t index)
    {
        if (!_in_bounds(_start, _length, _start + index, 1))
            R_STHROW(r_internal_exception, ("Array index outside the buffer bounds."));
        return *_current;
    }

    /// Returns the specified data type value at the current pointer position.
    template<typename T>
    R_API T read() const
    {
        if (!_in_bounds(_start, _length, _current, sizeof(T)))
        {
            R_STHROW(r_internal_exception, ("Attempt to read outside the buffer bounds. Start (%p), End (%p), Ptr (%p), Size (%llu)",
				     _start, _start + _length, _current, sizeof(T)));
        }
        return *(T*)_current;
    }

    /// Returns the specified data type value at the current pointer position AND increments
    /// the pointer past the data read.
    template<typename T>
    R_API T consume()
    {
        T value = read<T>();
        _current += sizeof(T);
        return value;
    }

    R_API void bulk_read( uint8_t* dst, uint32_t len )
    {
        if (!_in_bounds(_start, _length, _current, len))
        {
            R_STHROW(r_internal_exception, ("Attempt to read outside the buffer bounds. Start (%p), End (%p), Ptr (%p), Size (%llu)",
				     _start, _start + _length, _current, len));
        }

        memcpy( dst, _current, len );
        _current += len;
    }

    R_API const uint8_t* operator++()
    {
        return ++_current;
    }

    R_API const uint8_t* operator++(int)
    {
        return _current++;
    }

    R_API const uint8_t* operator--()
    {
        return --_current;
    }

    R_API const uint8_t* operator--(int)
    {
        return _current--;
    }

    R_API const uint8_t* operator+(size_t step) const
    {
        return _current + step;
    }

    R_API const uint8_t* operator-(size_t step) const
    {
        return _current - step;
    }

    R_API const uint8_t* operator+=(size_t step)
    {
        return _current += step;
    }

    R_API const uint8_t* operator-=(size_t step)
    {
        return _current -= step;
    }

    R_API bool operator==(const r_byte_ptr_ro& other) const
    {
        return other._current == _current;
    }

    R_API bool operator!=(const r_byte_ptr_ro& other) const
    {
        return other._current != _current;
    }

    R_API bool operator<=(const r_byte_ptr_ro& other) const
    {
        return _current <= other._current;
    }

    R_API bool operator>=(const r_byte_ptr_ro& other) const
    {
        return _current >= other._current;
    }

    R_API bool operator<(const r_byte_ptr_ro& other) const
    {
        return _current < other._current;
    }

    R_API bool operator>(const r_byte_ptr_ro& other) const
    {
        return _current > other._current;
    }

    /// Allows pointer to be cast to other pointer types only (not concrete types).
    template<typename to>
    R_API operator to() const
    {
        return _r_byte_ptr_ro_helper<to>::cast(_start, _current, _length);
    }

private:

    ///< The pointer to the start of the buffer.
    const uint8_t* _start;

    ///< The current pointer value.
    const uint8_t* _current;

    ///< The length of the buffer pointed to by '_start'
    size_t _length;
};

}

#endif

