
#ifndef r_utils_r_string_h
#define r_utils_r_string_h

#include "r_utils/r_macro.h"
#include <string>
#include <vector>
#include <cstdint>

namespace r_utils
{

namespace r_string_utils
{

R_API std::vector<std::string> split(const std::string& str, char delim);
R_API std::vector<std::string> split(const std::string& str, const std::string& delim);

R_API std::string join(const std::vector<std::string>& parts, char delim);
R_API std::string join(const std::vector<std::string>& parts, const std::string& delim);

R_API std::string format(const char* fmt, ...);
R_API std::string format(const char* fmt, va_list& args);

// Returns true if output was truncated (didn't fit in buf).
// Note: these methods are for specific performance use cases, if thats not
// what you need use the above format() method.
R_API bool format_buffer(char* buf, size_t size, const char* fmt, ...);
R_API bool format_buffer(char* buf, size_t size, const char* fmt, va_list& args);

R_API bool contains(const std::string& str, const std::string& target);

R_API std::string erase_all(const std::string& str, char delim);
R_API std::string erase_all(const std::string& str, const std::string& delim);
R_API std::string replace_all(const std::string& str, char toBeReplaced, char toReplaceWidth);
R_API std::string replace_all(const std::string& str, const std::string& toBeReplaced, const std::string& toReplaceWith);

R_API std::string to_lower(const std::string& str);
R_API std::string to_upper(const std::string& str);

R_API std::string uri_encode(const std::string& str);
R_API std::string uri_decode(const std::string& str);

R_API std::string to_base64( const void* source, size_t length );
R_API std::vector<uint8_t> from_base64(const std::string str);

R_API inline bool is_space(char a) { return (a == ' ' || a == '\n' || a == '\t' || a == '\r'); }

R_API bool is_integer(const std::string& str, bool canHaveSign=true);

R_API std::string lstrip(const std::string& str);
R_API std::string rstrip(const std::string& str);
R_API std::string strip(const std::string& str);
R_API std::string strip_eol(const std::string& str);

R_API bool starts_with(const std::string& str, const std::string& other);
R_API bool ends_with(const std::string& str, const std::string& other);

R_API int s_to_int(const std::string& s);
R_API unsigned int s_to_uint(const std::string& s);
R_API uint8_t s_to_uint8(const std::string& s);
R_API int8_t s_to_int8(const std::string& s);
R_API uint16_t s_to_uint16(const std::string& s);
R_API int16_t s_to_int16(const std::string& s);
R_API uint32_t s_to_uint32(const std::string& s);
R_API int32_t s_to_int32(const std::string& s);
R_API uint64_t s_to_uint64(const std::string& s);
R_API int64_t s_to_int64(const std::string& s);
R_API double s_to_double(const std::string& s);
R_API float s_to_float(const std::string& s);
R_API size_t s_to_size_t(const std::string& s);

R_API std::string int_to_s(int val);
R_API std::string uint_to_s(unsigned int val);
R_API std::string uint8_to_s(uint8_t val);
R_API std::string int8_to_s(int8_t val);
R_API std::string uint16_to_s(uint16_t val);
R_API std::string int16_to_s(int16_t val);
R_API std::string uint32_to_s(uint32_t val);
R_API std::string int32_to_s(int32_t val);
R_API std::string uint64_to_s(uint64_t val);
R_API std::string int64_to_s(int64_t val);
R_API std::string double_to_s(double val, int precision=6);
R_API std::string float_to_s(float val, int precision=6);
R_API std::string size_t_to_s(size_t val);

R_API std::string convert_utf16_string_to_multi_byte_string(const uint16_t* str);
R_API std::string convert_utf16_string_to_multi_byte_string(const uint16_t* str, size_t length);
R_API std::vector<uint16_t> convert_multi_byte_string_to_utf16_string(const std::string& str);
R_API std::string convert_utf32_string_to_multi_byte_string(const uint32_t* str);
R_API std::string convert_utf32_string_to_multi_byte_string(const uint32_t* str, size_t length);
R_API std::vector<uint32_t> convert_multi_byte_string_to_utf32_string(const std::string& str);
R_API std::string convert_wide_string_to_multi_byte_string(const wchar_t* str);
R_API std::string convert_wide_string_to_multi_byte_string(const wchar_t* str, size_t length);
R_API std::wstring convert_multi_byte_string_to_wide_string(const std::string& str);

}

}

#endif
