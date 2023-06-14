
#include "r_pipeline/r_gst_buffer.h"

using namespace r_pipeline;

r_gst_buffer::r_gst_buffer() :
    _buffer(nullptr),
    _size(0)
{
}

r_gst_buffer::r_gst_buffer(GstBuffer* buffer) :
    _buffer(buffer),
    _size(0)
{
    if(!_buffer)
        R_THROW(("Invalid buffer!"));

    gst_buffer_ref(_buffer);

    auto mi = map(MT_READ);
    _size = mi.size();
}

r_gst_buffer::r_gst_buffer(const uint8_t* data, size_t size)
{
    _buffer = gst_buffer_new_and_alloc(size);
    if(!_buffer)
        R_THROW(("Unable to allocate buffer!"));
    auto mi = map(MT_WRITE);
    memcpy(mi.data(), data, size);
    _size = size;
}

r_gst_buffer::r_gst_buffer(const r_gst_buffer& obj) :
    _buffer(obj._buffer),
    _size(obj._size)
{
    gst_buffer_ref(_buffer);
}

r_gst_buffer::r_gst_buffer(r_gst_buffer&& obj) :
    _buffer(std::move(obj._buffer)),
    _size(std::move(obj._size))
{
    obj._buffer = nullptr;
}

r_gst_buffer::~r_gst_buffer() noexcept
{
    _clear();
}

r_gst_buffer& r_gst_buffer::operator=(const r_gst_buffer& obj)
{
    if(this != &obj)
    {
        _clear();

        _buffer = obj._buffer;
        _size = obj._size;
        gst_buffer_ref(_buffer);
    }

    return *this;
}

r_gst_buffer& r_gst_buffer::operator=(r_gst_buffer&& obj)
{
    if(this != &obj)
    {
        _clear();

        _buffer = std::move(obj._buffer);
        _size = std::move(obj._size);
        obj._buffer = nullptr;
    }

    return *this;
}

r_gst_buffer::r_map_info r_gst_buffer::map(r_map_type type) const
{
    return r_map_info(this, type);
}

void r_gst_buffer::_clear() noexcept
{
    if(_buffer)
    {
        gst_buffer_unref(_buffer);
        _buffer = nullptr;
        _size = 0;
    }
}
