#include "error_handling.h"
#include "imgui/imgui.h"
#include "r_http/r_client_request.h"
#include "r_http/r_client_response.h"
#include "r_utils/r_socket.h"
#include <regex>
#include <thread>
#include <chrono>
#include <limits>
#include <algorithm>
#include <cmath>

namespace vision
{

namespace gl_safe
{

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

    bool create_texture(GLuint* texture_id, int width, int height, const void* data)
    {
        if (!texture_id || width <= 0 || height <= 0)
        {
            R_LOG_ERROR("Invalid parameters for texture creation: width=%d, height=%d, texture_id=%p", 
                       width, height, texture_id);
            return false;
        }
        
        GL_CHECK(glGenTextures(1, texture_id));
        if (*texture_id == 0)
        {
            R_LOG_ERROR("Failed to generate texture ID");
            return false;
        }
        
        GL_CHECK(glBindTexture(GL_TEXTURE_2D, *texture_id));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

        GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data));
        
        GLenum error = glGetError();
        if (error != GL_NO_ERROR)
        {
            R_LOG_ERROR("Failed to create texture: OpenGL error 0x%x", error);
            glDeleteTextures(1, texture_id);
            *texture_id = 0;
            return false;
        }
        
        return true;
    }
    
    bool create_texture_rgba(GLuint* texture_id, int width, int height, const void* data)
    {
        if (!texture_id || width <= 0 || height <= 0)
        {
            R_LOG_ERROR("Invalid parameters for RGBA texture creation: width=%d, height=%d, texture_id=%p", 
                       width, height, texture_id);
            return false;
        }
        
        GL_CHECK(glGenTextures(1, texture_id));
        if (*texture_id == 0)
        {
            R_LOG_ERROR("Failed to generate texture ID");
            return false;
        }
        
        GL_CHECK(glBindTexture(GL_TEXTURE_2D, *texture_id));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

        GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data));
        
        GLenum error = glGetError();
        if (error != GL_NO_ERROR)
        {
            R_LOG_ERROR("Failed to create RGBA texture: OpenGL error 0x%x", error);
            glDeleteTextures(1, texture_id);
            *texture_id = 0;
            return false;
        }
        
        return true;
    }
    
    bool delete_texture(GLuint texture_id)
    {
        if (texture_id == 0)
        {
            return true; // Nothing to delete
        }
        
        GL_CHECK(glDeleteTextures(1, &texture_id));
        return glGetError() == GL_NO_ERROR;
    }
    
    bool update_texture(GLuint texture_id, int width, int height, const void* data)
    {
        if (texture_id == 0 || !data || width <= 0 || height <= 0)
        {
            R_LOG_ERROR("Invalid parameters for texture update: texture_id=%u, width=%d, height=%d, data=%p", 
                       texture_id, width, height, data);
            return false;
        }
        
        GL_CHECK(glBindTexture(GL_TEXTURE_2D, texture_id));
        GL_CHECK(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, data));
        
        return glGetError() == GL_NO_ERROR;
    }
}

namespace imgui_safe
{
    bool begin_window(const char* name, bool* p_open, int flags)
    {
        if (!name)
        {
            R_LOG_ERROR("NULL window name passed to begin_window");
            return false;
        }
        
        try
        {
            return ImGui::Begin(name, p_open, flags);
        }
        catch(const std::exception& e)
        {
            R_LOG_ERROR("Exception in ImGui::Begin for window '%s': %s", name, e.what());
            return false;
        }
        catch(...)
        {
            R_LOG_ERROR("Unknown exception in ImGui::Begin for window '%s'", name);
            return false;
        }
    }
    
    void end_window_safe()
    {
        try
        {
            ImGui::End();
        }
        catch(const std::exception& e)
        {
            R_LOG_ERROR("Exception in ImGui::End: %s", e.what());
        }
        catch(...)
        {
            R_LOG_ERROR("Unknown exception in ImGui::End");
        }
    }
    
    bool button_safe(const char* label, float width, float height)
    {
        if (!label)
        {
            R_LOG_ERROR("NULL label passed to button_safe");
            return false;
        }
        
        try
        {
            if (width > 0 && height > 0)
                return ImGui::Button(label, ImVec2(width, height));
            else
                return ImGui::Button(label);
        }
        catch(const std::exception& e)
        {
            R_LOG_ERROR("Exception in ImGui::Button for '%s': %s", label, e.what());
            return false;
        }
        catch(...)
        {
            R_LOG_ERROR("Unknown exception in ImGui::Button for '%s'", label);
            return false;
        }
    }
}

namespace network_safe
{
    http_result http_get_with_retry(const std::string& url, int max_retries, int timeout_ms)
    {
        http_result result;
        result.success = false;
        result.status_code = 0;
        
        if (url.empty())
        {
            result.error_message = "Empty URL provided";
            return result;
        }
        
        // Parse URL to extract host, port, and path
        // Simple parsing for http://host:port/path format
        std::string host = "127.0.0.1";
        int port = 10080;
        std::string path = "/";
        
        // Extract path from URL for now (basic implementation)
        size_t path_start = url.find("/", 7); // Skip "http://"
        if (path_start != std::string::npos)
        {
            path = url.substr(path_start);
        }
        
        for (int attempt = 0; attempt <= max_retries; ++attempt)
        {
            try
            {
                r_utils::r_socket sok;
                sok.connect(host, port);
                
                r_http::r_client_request req(host, port);
                req.set_uri(path);
                req.write_request(sok);
                
                r_http::r_client_response response;
                response.read_response(sok);
                
                result.status_code = response.is_success() ? 200 : 500; // Simplified status handling
                auto body_maybe = response.get_body_as_string();
                if (!body_maybe.is_null())
                    result.response_body = body_maybe.value();
                else
                    result.response_body = "";
                
                if (response.is_success())
                {
                    result.success = true;
                    return result;
                }
                else
                {
                    result.error_message = "HTTP request failed";
                    R_LOG_ERROR("HTTP request failed (attempt %d/%d)", 
                               attempt + 1, max_retries + 1);
                }
            }
            catch(const std::exception& e)
            {
                result.error_message = std::string("Network exception: ") + e.what();
                R_LOG_ERROR("Network exception on attempt %d/%d: %s", 
                           attempt + 1, max_retries + 1, e.what());
            }
            catch(...)
            {
                result.error_message = "Unknown network error";
                R_LOG_ERROR("Unknown network exception on attempt %d/%d", 
                           attempt + 1, max_retries + 1);
            }
            
            // Don't sleep after the last attempt
            if (attempt < max_retries)
            {
                int delay_ms = 500 * (1 << attempt); // Exponential backoff: 500ms, 1s, 2s, etc.
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            }
        }
        
        return result;
    }
}

namespace state_validate
{
    bool is_valid_timerange(const std::chrono::system_clock::time_point& start, 
                           const std::chrono::system_clock::time_point& end)
    {
        if (start >= end)
        {
            R_LOG_ERROR("Invalid time range: start time >= end time");
            return false;
        }
        
        auto duration = end - start;
        auto hours = std::chrono::duration_cast<std::chrono::hours>(duration).count();
        
        // Reasonable limits: between 1 minute and 24 hours
        if (hours > 24)
        {
            R_LOG_ERROR("Time range too large: %ld hours", hours);
            return false;
        }
        
        auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration).count();
        if (minutes < 1)
        {
            R_LOG_ERROR("Time range too small: %ld minutes", minutes);
            return false;
        }
        
        return true;
    }
    
    bool is_valid_playhead_position(int position, int min_pos, int max_pos)
    {
        if (position < min_pos || position > max_pos)
        {
            R_LOG_ERROR("Invalid playhead position: %d (valid range: %d-%d)", 
                       position, min_pos, max_pos);
            return false;
        }
        return true;
    }
    
    bool is_valid_ip_address(const std::string& ip)
    {
        if (ip.empty())
        {
            R_LOG_ERROR("Empty IP address");
            return false;
        }
        
        // Basic IPv4 validation using regex
        std::regex ipv4_pattern(R"(^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$)");
        std::smatch match;
        
        if (!std::regex_match(ip, match, ipv4_pattern))
        {
            R_LOG_ERROR("Invalid IP address format: %s", ip.c_str());
            return false;
        }
        
        // Check each octet is in valid range (0-255)
        for (int i = 1; i <= 4; ++i)
        {
            int octet = std::stoi(match[i].str());
            if (octet < 0 || octet > 255)
            {
                R_LOG_ERROR("Invalid IP address octet: %d in %s", octet, ip.c_str());
                return false;
            }
        }
        
        return true;
    }
    
    bool is_valid_camera_id(const std::string& camera_id)
    {
        if (camera_id.empty())
        {
            R_LOG_ERROR("Empty camera ID");
            return false;
        }
        
        if (camera_id.length() > 64)
        {
            R_LOG_ERROR("Camera ID too long: %zu characters", camera_id.length());
            return false;
        }
        
        // Basic validation: alphanumeric and some special characters
        std::regex camera_pattern(R"(^[a-zA-Z0-9_-]+$)");
        if (!std::regex_match(camera_id, camera_pattern))
        {
            R_LOG_ERROR("Invalid camera ID format: %s", camera_id.c_str());
            return false;
        }
        
        return true;
    }
    
    bool is_valid_timerange_minutes(int minutes)
    {
        if (minutes <= 0)
        {
            R_LOG_ERROR("Invalid timerange minutes: %d (must be positive)", minutes);
            return false;
        }
        
        if (minutes > 1440) // More than 24 hours
        {
            R_LOG_ERROR("Timerange too large: %d minutes (max 1440)", minutes);
            return false;
        }
        
        return true;
    }
    
    bool is_valid_frame_dimensions(uint16_t width, uint16_t height)
    {
        if (width == 0 || height == 0)
        {
            R_LOG_ERROR("Invalid frame dimensions: %dx%d (cannot be zero)", width, height);
            return false;
        }
        
        if (width > 4096 || height > 4096)
        {
            R_LOG_ERROR("Frame dimensions too large: %dx%d (max 4096x4096)", width, height);
            return false;
        }
        
        // Check for reasonable aspect ratios (prevent extremely narrow images)
        float aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
        if (aspect_ratio < 0.1f || aspect_ratio > 10.0f)
        {
            R_LOG_ERROR("Invalid aspect ratio: %f (width=%d, height=%d)", aspect_ratio, width, height);
            return false;
        }
        
        return true;
    }
    
    bool is_valid_buffer_size(size_t buffer_size, uint16_t width, uint16_t height, int channels)
    {
        if (channels <= 0 || channels > 4)
        {
            R_LOG_ERROR("Invalid channel count: %d (must be 1-4)", channels);
            return false;
        }
        
        if (!is_valid_frame_dimensions(width, height))
        {
            return false; // Already logged
        }
        
        size_t expected_size = static_cast<size_t>(width) * height * channels;
        
        if (buffer_size < expected_size)
        {
            R_LOG_ERROR("Buffer too small: %zu bytes (expected %zu for %dx%d with %d channels)", 
                       buffer_size, expected_size, width, height, channels);
            return false;
        }
        
        // Allow some extra bytes but not too much (indicates possible corruption)
        size_t max_size = expected_size + (expected_size / 10); // 10% tolerance
        if (buffer_size > max_size)
        {
            R_LOG_ERROR("Buffer suspiciously large: %zu bytes (expected ~%zu)", buffer_size, expected_size);
            return false;
        }
        
        return true;
    }
    
    bool is_valid_texture_id(GLuint texture_id)
    {
        if (texture_id == 0)
        {
            R_LOG_ERROR("Invalid texture ID: 0");
            return false;
        }
        
        // Check if it's a valid OpenGL texture
        GLboolean is_texture = glIsTexture(texture_id);
        if (is_texture == GL_FALSE)
        {
            R_LOG_ERROR("Invalid OpenGL texture ID: %u", texture_id);
            return false;
        }
        
        return true;
    }
    
    bool is_valid_coordinates(float x, float y, float max_x, float max_y)
    {
        if (std::isnan(x) || std::isnan(y))
        {
            R_LOG_ERROR("NaN coordinates: x=%f, y=%f", x, y);
            return false;
        }
        
        if (std::isinf(x) || std::isinf(y))
        {
            R_LOG_ERROR("Infinite coordinates: x=%f, y=%f", x, y);
            return false;
        }
        
        if (x < 0.0f || y < 0.0f)
        {
            R_LOG_ERROR("Negative coordinates: x=%f, y=%f", x, y);
            return false;
        }
        
        if (x > max_x || y > max_y)
        {
            R_LOG_ERROR("Coordinates out of bounds: x=%f, y=%f (max: %f, %f)", x, y, max_x, max_y);
            return false;
        }
        
        return true;
    }
    
    bool is_valid_window_dimensions(uint16_t width, uint16_t height)
    {
        if (width < 100 || height < 100)
        {
            R_LOG_ERROR("Window too small: %dx%d (minimum 100x100)", width, height);
            return false;
        }
        
        if (width > 7680 || height > 4320) // 8K resolution
        {
            R_LOG_ERROR("Window too large: %dx%d (maximum 7680x4320)", width, height);
            return false;
        }
        
        return true;
    }
    
    bool is_valid_layout_ratios(float left_ratio, float center_ratio, float right_ratio)
    {
        if (left_ratio < 0.0f || center_ratio < 0.0f || right_ratio < 0.0f)
        {
            R_LOG_ERROR("Negative layout ratios: left=%f, center=%f, right=%f", 
                       left_ratio, center_ratio, right_ratio);
            return false;
        }
        
        float total = left_ratio + center_ratio + right_ratio;
        if (total <= 0.0f)
        {
            R_LOG_ERROR("Layout ratios sum to zero or negative: %f", total);
            return false;
        }
        
        // Allow some tolerance but ratios should be reasonable
        if (left_ratio > 0.5f || center_ratio > 0.9f || right_ratio > 0.5f)
        {
            R_LOG_ERROR("Layout ratios unreasonable: left=%f, center=%f, right=%f", 
                       left_ratio, center_ratio, right_ratio);
            return false;
        }
        
        return true;
    }
    
    bool is_valid_rtsp_url(const std::string& url)
    {
        if (url.empty())
        {
            R_LOG_ERROR("Empty RTSP URL");
            return false;
        }
        
        if (url.length() > 512)
        {
            R_LOG_ERROR("RTSP URL too long: %zu characters", url.length());
            return false;
        }
        
        // Basic RTSP URL validation
        std::regex rtsp_pattern(R"(^rtsp://[a-zA-Z0-9.-]+:[0-9]+/.+$)");
        if (!std::regex_match(url, rtsp_pattern))
        {
            R_LOG_ERROR("Invalid RTSP URL format: %s", url.c_str());
            return false;
        }
        
        return true;
    }
    
    bool is_valid_stream_name(const std::string& name)
    {
        if (name.empty())
        {
            R_LOG_ERROR("Empty stream name");
            return false;
        }
        
        if (name.length() > 64)
        {
            R_LOG_ERROR("Stream name too long: %zu characters", name.length());
            return false;
        }
        
        // Allow alphanumeric, spaces, and some special characters
        std::regex name_pattern(R"(^[a-zA-Z0-9 _.-]+$)");
        if (!std::regex_match(name, name_pattern))
        {
            R_LOG_ERROR("Invalid stream name format: %s", name.c_str());
            return false;
        }
        
        return true;
    }
    
    bool is_valid_vector_index(size_t index, size_t container_size)
    {
        if (index >= container_size)
        {
            R_LOG_ERROR("Vector index out of bounds: %zu >= %zu", index, container_size);
            return false;
        }
        
        return true;
    }
    
    std::optional<int64_t> safe_duration_millis(const std::chrono::system_clock::time_point& start,
                                               const std::chrono::system_clock::time_point& end)
    {
        if (start >= end)
        {
            R_LOG_ERROR("Invalid time range: start >= end");
            return std::nullopt;
        }
        
        auto duration = end - start;
        
        // Check for overflow when converting to milliseconds
        auto max_duration = std::chrono::milliseconds(std::numeric_limits<int64_t>::max());
        if (duration > max_duration)
        {
            R_LOG_ERROR("Duration too large for milliseconds conversion");
            return std::nullopt;
        }
        
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    }
    
    std::optional<float> safe_divide(float numerator, float denominator)
    {
        if (denominator == 0.0f)
        {
            R_LOG_ERROR("Division by zero: %f / 0", numerator);
            return std::nullopt;
        }
        
        if (std::isnan(numerator) || std::isnan(denominator))
        {
            R_LOG_ERROR("NaN in division: %f / %f", numerator, denominator);
            return std::nullopt;
        }
        
        if (std::isinf(denominator))
        {
            R_LOG_ERROR("Infinite denominator in division: %f / %f", numerator, denominator);
            return std::nullopt;
        }
        
        float result = numerator / denominator;
        if (std::isnan(result) || std::isinf(result))
        {
            R_LOG_ERROR("Invalid division result: %f / %f = %f", numerator, denominator, result);
            return std::nullopt;
        }
        
        return result;
    }
    
    std::optional<double> safe_divide(double numerator, double denominator)
    {
        if (denominator == 0.0)
        {
            R_LOG_ERROR("Division by zero: %f / 0", numerator);
            return std::nullopt;
        }
        
        if (std::isnan(numerator) || std::isnan(denominator))
        {
            R_LOG_ERROR("NaN in division: %f / %f", numerator, denominator);
            return std::nullopt;
        }
        
        if (std::isinf(denominator))
        {
            R_LOG_ERROR("Infinite denominator in division: %f / %f", numerator, denominator);
            return std::nullopt;
        }
        
        double result = numerator / denominator;
        if (std::isnan(result) || std::isinf(result))
        {
            R_LOG_ERROR("Invalid division result: %f / %f = %f", numerator, denominator, result);
            return std::nullopt;
        }
        
        return result;
    }
    
    bool is_safe_addition(int64_t a, int64_t b)
    {
        // Check for overflow
        if (b > 0 && a > std::numeric_limits<int64_t>::max() - b)
        {
            R_LOG_ERROR("Integer overflow in addition: %lld + %lld", a, b);
            return false;
        }
        
        // Check for underflow
        if (b < 0 && a < std::numeric_limits<int64_t>::min() - b)
        {
            R_LOG_ERROR("Integer underflow in addition: %lld + %lld", a, b);
            return false;
        }
        
        return true;
    }
    
    bool is_safe_multiplication(int64_t a, int64_t b)
    {
        if (a == 0 || b == 0)
        {
            return true; // Always safe
        }
        
        // Check for overflow
        if (a > 0 && b > 0 && a > std::numeric_limits<int64_t>::max() / b)
        {
            R_LOG_ERROR("Integer overflow in multiplication: %lld * %lld", a, b);
            return false;
        }
        
        if (a < 0 && b < 0 && a < std::numeric_limits<int64_t>::max() / b)
        {
            R_LOG_ERROR("Integer overflow in multiplication: %lld * %lld", a, b);
            return false;
        }
        
        if ((a > 0 && b < 0 && b < std::numeric_limits<int64_t>::min() / a) ||
            (a < 0 && b > 0 && a < std::numeric_limits<int64_t>::min() / b))
        {
            R_LOG_ERROR("Integer underflow in multiplication: %lld * %lld", a, b);
            return false;
        }
        
        return true;
    }
}

} // namespace vision