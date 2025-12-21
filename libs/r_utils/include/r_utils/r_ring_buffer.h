
#ifndef r_utils_r_ring_buffer_h
#define r_utils_r_ring_buffer_h

#include <vector>
#include <stdexcept>
#include <cstddef>

namespace r_utils {

template<typename T>
class r_ring_buffer final
{
public:
    r_ring_buffer(size_t capacity) :
        _buffer(capacity),
        _head(0),
        _tail(0),
        _size(0),
        _capacity(capacity)
    {
        if(capacity == 0)
            throw std::invalid_argument("r_ring_buffer capacity must be > 0");
    }

    r_ring_buffer(const r_ring_buffer&) = default;
    r_ring_buffer(r_ring_buffer&&) = default;
    r_ring_buffer& operator=(const r_ring_buffer&) = default;
    r_ring_buffer& operator=(r_ring_buffer&&) = default;

    void push(T item)
    {
        _buffer[_head] = std::move(item);

        if(_size == _capacity)
        {
            // Buffer is full, advance tail (overwrite oldest)
            _tail = (_tail + 1) % _capacity;
        }
        else
        {
            ++_size;
        }

        _head = (_head + 1) % _capacity;
    }

    size_t size() const { return _size; }
    size_t capacity() const { return _capacity; }
    bool full() const { return _size == _capacity; }
    bool empty() const { return _size == 0; }

    // Access by age: 0 = oldest, size()-1 = newest
    const T& at(size_t idx) const
    {
        if(idx >= _size)
            throw std::out_of_range("r_ring_buffer index out of range");

        size_t actual_idx = (_tail + idx) % _capacity;
        return _buffer[actual_idx];
    }

    // Access newest item (convenience method)
    const T& newest() const
    {
        if(_size == 0)
            throw std::out_of_range("r_ring_buffer is empty");

        size_t newest_idx = (_head == 0) ? (_capacity - 1) : (_head - 1);
        return _buffer[newest_idx];
    }

    // Access oldest item (convenience method)
    const T& oldest() const
    {
        if(_size == 0)
            throw std::out_of_range("r_ring_buffer is empty");

        return _buffer[_tail];
    }

    // Iteration from oldest to newest
    template<typename F>
    void for_each(F&& func) const
    {
        for(size_t i = 0; i < _size; ++i)
        {
            size_t actual_idx = (_tail + i) % _capacity;
            func(_buffer[actual_idx]);
        }
    }

    // Count items matching predicate
    template<typename P>
    size_t count_if(P&& predicate) const
    {
        size_t count = 0;
        for(size_t i = 0; i < _size; ++i)
        {
            size_t actual_idx = (_tail + i) % _capacity;
            if(predicate(_buffer[actual_idx]))
                ++count;
        }
        return count;
    }

    // Check if last N items all match predicate
    // Returns false if size < n
    template<typename P>
    bool last_n_match(size_t n, P&& predicate) const
    {
        if(n == 0)
            return true;

        if(_size < n)
            return false;

        // Start from newest and work backwards
        for(size_t i = 0; i < n; ++i)
        {
            size_t age_from_newest = _size - 1 - i;
            size_t actual_idx = (_tail + age_from_newest) % _capacity;
            if(!predicate(_buffer[actual_idx]))
                return false;
        }
        return true;
    }

    void clear()
    {
        _head = 0;
        _tail = 0;
        _size = 0;
    }

private:
    std::vector<T> _buffer;
    size_t _head;      // Next write position
    size_t _tail;      // Oldest item position
    size_t _size;      // Current number of items
    size_t _capacity;
};

} // namespace r_utils

#endif
