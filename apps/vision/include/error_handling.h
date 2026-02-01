#ifndef __vision_error_handling_h
#define __vision_error_handling_h

#include "r_utils/r_logger.h"
#include <SDL.h>
#include <functional>
#include <string>
#include <chrono>
#include <optional>
#include <cstdint>
#include <memory>
#include "r_ui_utils/texture.h"

namespace vision
{

// SDL error checking helper
namespace sdl_safe
{
    inline bool check_error(const char* operation)
    {
        const char* error = SDL_GetError();
        if (*error)
        {
            R_LOG_ERROR("%s: %s", operation, error);
            SDL_ClearError();
            return false;
        }
        return true;
    }
}

// ImGui operation safety wrapper
namespace imgui_safe
{
    bool begin_window(const char* name, bool* p_open = nullptr, int flags = 0);
    void end_window_safe();
    bool button_safe(const char* label, float width = 0, float height = 0);
}

// Network operation error handling
namespace network_safe
{
    struct http_result
    {
        bool success;
        int status_code;
        std::string error_message;
        std::string response_body;
    };

    http_result http_get_with_retry(const std::string& url, int max_retries = 3, int timeout_ms = 5000);
}

// State validation utilities
namespace state_validate
{
    // Time and range validation
    bool is_valid_timerange(const std::chrono::system_clock::time_point& start,
                           const std::chrono::system_clock::time_point& end);
    bool is_valid_playhead_position(int position, int min_pos = 0, int max_pos = 1000);
    bool is_valid_timerange_minutes(int minutes);

    // Video data validation
    bool is_valid_frame_dimensions(uint16_t width, uint16_t height);
    bool is_valid_buffer_size(size_t buffer_size, uint16_t width, uint16_t height, int channels = 3);
    bool is_valid_texture(const std::shared_ptr<r_ui_utils::texture>& tex);

    // UI layout validation
    bool is_valid_coordinates(float x, float y, float max_x = 10000.0f, float max_y = 10000.0f);
    bool is_valid_window_dimensions(uint16_t width, uint16_t height);
    bool is_valid_layout_ratios(float left_ratio, float center_ratio, float right_ratio = 0.0f);

    // Configuration validation
    bool is_valid_ip_address(const std::string& ip);
    bool is_valid_camera_id(const std::string& camera_id);
    bool is_valid_rtsp_url(const std::string& url);
    bool is_valid_stream_name(const std::string& name);

    // Data structure validation
    bool is_valid_vector_index(size_t index, size_t container_size);

    // Mathematical operation safety
    std::optional<int64_t> safe_duration_millis(const std::chrono::system_clock::time_point& start,
                                               const std::chrono::system_clock::time_point& end);
    std::optional<float> safe_divide(float numerator, float denominator);
    std::optional<double> safe_divide(double numerator, double denominator);
    bool is_safe_addition(int64_t a, int64_t b);
    bool is_safe_multiplication(int64_t a, int64_t b);
}

// Safe container access utilities
namespace container_safe
{
    template<typename Container>
    auto safe_at(Container& container, size_t index) ->
        std::optional<std::reference_wrapper<typename Container::value_type>>
    {
        if (index >= container.size()) {
            R_LOG_ERROR("Container access out of bounds: index %zu >= size %zu", index, container.size());
            return std::nullopt;
        }
        return std::ref(container[index]);
    }

    template<typename Container>
    auto safe_at(const Container& container, size_t index) ->
        std::optional<std::reference_wrapper<const typename Container::value_type>>
    {
        if (index >= container.size()) {
            R_LOG_ERROR("Container access out of bounds: index %zu >= size %zu", index, container.size());
            return std::nullopt;
        }
        return std::ref(container[index]);
    }

    template<typename Map>
    auto safe_find(const Map& map, const typename Map::key_type& key) ->
        std::optional<std::reference_wrapper<const typename Map::mapped_type>>
    {
        auto it = map.find(key);
        if (it == map.end()) {
            return std::nullopt;
        }
        return std::ref(it->second);
    }
}

// Error context for better debugging
struct error_context
{
    std::string function_name;
    std::string file_name;
    int line_number;
    std::string additional_info;

    error_context(const char* func, const char* file, int line, const std::string& info = "")
        : function_name(func), file_name(file), line_number(line), additional_info(info) {}
};

#define ERROR_CONTEXT(info) error_context(__FUNCTION__, __FILE__, __LINE__, info)

// Exception-safe operation wrapper
template<typename T>
class safe_operation
{
public:
    static bool execute(std::function<T()> operation, T& result, const error_context& ctx)
    {
        try
        {
            result = operation();
            return true;
        }
        catch(const std::exception& e)
        {
            R_LOG_ERROR("Exception in %s at %s:%d - %s. Additional info: %s",
                       ctx.function_name.c_str(), ctx.file_name.c_str(), ctx.line_number,
                       e.what(), ctx.additional_info.c_str());
            return false;
        }
        catch(...)
        {
            R_LOG_ERROR("Unknown exception in %s at %s:%d. Additional info: %s",
                       ctx.function_name.c_str(), ctx.file_name.c_str(), ctx.line_number,
                       ctx.additional_info.c_str());
            return false;
        }
    }
};

// Specialized version for void operations
template<>
class safe_operation<void>
{
public:
    static bool execute(std::function<void()> operation, const error_context& ctx)
    {
        try
        {
            operation();
            return true;
        }
        catch(const std::exception& e)
        {
            R_LOG_ERROR("Exception in %s at %s:%d - %s. Additional info: %s",
                       ctx.function_name.c_str(), ctx.file_name.c_str(), ctx.line_number,
                       e.what(), ctx.additional_info.c_str());
            return false;
        }
        catch(...)
        {
            R_LOG_ERROR("Unknown exception in %s at %s:%d. Additional info: %s",
                       ctx.function_name.c_str(), ctx.file_name.c_str(), ctx.line_number,
                       ctx.additional_info.c_str());
            return false;
        }
    }
};

#define SAFE_EXECUTE(operation, result, info) \
    safe_operation<decltype(result)>::execute([&]() { return operation; }, result, ERROR_CONTEXT(info))

#define SAFE_EXECUTE_VOID(operation, info) \
    safe_operation<void>::execute([&]() { operation; }, ERROR_CONTEXT(info))

} // namespace vision

#endif
