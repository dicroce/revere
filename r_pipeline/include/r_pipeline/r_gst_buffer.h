#ifndef r_pipeline_r_gst_buffer_h
#define r_pipeline_r_gst_buffer_h

#include "r_utils/r_exception.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"

#ifdef IS_WINDOWS
#pragma warning( push )
#pragma warning( disable : 4244 )
#endif
#include <gst/gst.h>
#include <gst/rtsp/gstrtspmessage.h>
#include <gst/sdp/gstsdpmessage.h>
#include <gst/app/gstappsink.h>
#ifdef IS_WINDOWS
#pragma warning( pop )
#endif

namespace r_pipeline
{

class r_gst_buffer final
{
public:
    enum r_map_type
    {
        MT_READ,
        MT_WRITE
    };

    class r_map_info final
    {
        friend class r_gst_buffer;
    public:
        R_API r_map_info() = delete;
        R_API r_map_info(const r_gst_buffer* buffer, r_map_type type) :
            _buffer(buffer),
            _type(type),
            _info()
        {
            GstMapInfo info;
            if(!gst_buffer_map(_buffer->_buffer, &info, (type==MT_READ)?GST_MAP_READ:GST_MAP_WRITE))
                R_THROW(("Unable to map buffer!"));
            _info.set_value(info);
        }
        R_API r_map_info(const r_map_info&) = delete;
        R_API r_map_info(r_map_info&& obj) noexcept :
            _buffer(std::move(obj._buffer)),
            _type(std::move(obj._type)),
            _info(std::move(obj._info))
        {
        }
        R_API ~r_map_info() noexcept
        {
            _clear();
        }

        R_API r_map_info& operator=(const r_map_info&) = delete;
        R_API r_map_info& operator=(r_map_info&& obj) noexcept
        {
            if(this != &obj)
            {
                _clear();

                _buffer = std::move(obj._buffer);
                _type = std::move(obj._type);
                _info = std::move(obj._info);
            }

            return *this;
        }

        R_API uint8_t* data() const noexcept {return _info.value().data;}
        R_API size_t size() const noexcept {return _info.value().size;}

    private:
        void _clear() noexcept
        {
            if(!_info.is_null())
            {
                auto info = _info.value();
                gst_buffer_unmap(_buffer->_buffer, &info);
                _info.clear();
            }
        }

        const r_gst_buffer* _buffer;
        r_map_type _type;
        r_utils::r_nullable<GstMapInfo> _info;
    };

    R_API r_gst_buffer();
    R_API r_gst_buffer(GstBuffer* buffer);
    R_API r_gst_buffer(const uint8_t* data, size_t size);
    R_API r_gst_buffer(const r_gst_buffer& obj);
    R_API r_gst_buffer(r_gst_buffer&& obj);
    R_API ~r_gst_buffer() noexcept;

    R_API r_gst_buffer& operator=(const r_gst_buffer& obj);
    R_API r_gst_buffer& operator=(r_gst_buffer&& obj);

    R_API r_map_info map(r_map_type type) const;

    R_API bool key() const noexcept {return !GST_BUFFER_FLAG_IS_SET(_buffer, GST_BUFFER_FLAG_DELTA_UNIT);}

    R_API GstBuffer* get() const noexcept {return _buffer;}

    R_API size_t size() const noexcept {return _size;}

private:
    void _clear() noexcept;

    GstBuffer* _buffer;
    size_t _size;
};

}


#endif

