
#include "r_utils/r_string_utils.h"
#include "r_utils/r_exception.h"
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <array>
#include <algorithm>
#include <inttypes.h>
#include <sstream>

using namespace r_utils;
using namespace std;

#ifdef IS_WINDOWS
const static size_t WIDE_CHAR_SIZE = 2;
#else
const static size_t WIDE_CHAR_SIZE = 4;
#endif

vector<string> r_utils::r_string_utils::split(const string& str, char delim)
{
    return split(str, string(&delim, 1));
}

vector<string> r_utils::r_string_utils::split(const string& str, const string& delim)
{
    vector<string> parts;

    size_t begin = 0;
    size_t end = 0;

    auto delimLen = delim.length();

    while(true)
    {
        end = str.find(delim, begin);

        if(end == string::npos)
        {
            if(str.begin()+begin != str.end())
                parts.emplace_back(str.begin()+begin, str.end());
            break;
        }

        if(end != begin)
            parts.emplace_back(str.begin()+begin, str.begin()+end);

        begin = end + delimLen;
    }

    return parts;
}

string r_utils::r_string_utils::join(const vector<string>& parts, char delim)
{
    string result;
    for(auto b = begin(parts), e = end(parts); b != e; ++b)
        result += *b + ((next(b)!=e)?string(1,delim):"");
    return result;
}

string r_utils::r_string_utils::join(const vector<string>& parts, const string& delim)
{
    string result;
    for(auto b = begin(parts), e = end(parts); b != e; ++b)
        result += *b + ((next(b)!=e)?delim:"");
    return result;
}

string r_utils::r_string_utils::format(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const string result = format(fmt, args);
    va_end(args);
    return result;
}

string r_utils::r_string_utils::format(const char* fmt, va_list& args)
{
    va_list newargs;
    va_copy(newargs, args);

    int chars_written = vsnprintf(nullptr, 0, fmt, newargs);
    int len = chars_written + 1;

    vector<char> str(len);

    va_end(newargs);

    va_copy(newargs, args);
    vsnprintf(&str[0], len, fmt, newargs);

    va_end(newargs);

    return string(&str[0]);
}

bool r_utils::r_string_utils::format_buffer(char* buf, size_t size, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    auto result = format_buffer(buf, size, fmt, args);
    va_end(args);
    return result;
}

bool r_utils::r_string_utils::format_buffer(char* buf, size_t size, const char* fmt, va_list& args)
{
    auto result = vsnprintf(buf, size, fmt, args);
    if(result < 0)
        R_STHROW(r_internal_exception, ("Unable to format string buffer."));
    return ((size_t)result >= size);
}

bool r_utils::r_string_utils::contains(const string& str, const string& target)
{
    return (str.find(target) != string::npos) ? true : false;
}

string r_utils::r_string_utils::erase_all(const string& str, char delim)
{
    return erase_all(str, string(1, delim));
}

string r_utils::r_string_utils::erase_all(const string& str, const string& delim)
{
    auto splitList = r_utils::r_string_utils::split( str, delim );

    string output;
    for(auto& p : splitList)
        output += p;

    return output;
}

string r_utils::r_string_utils::replace_all(const string& str, char toBeReplaced, char toReplaceWith)
{
    return replace_all(str, string(1, toBeReplaced), string(1, toReplaceWith));
}

string r_utils::r_string_utils::replace_all(const string& str, const string& toBeReplaced, const string& toReplaceWith)
{
    size_t found = str.find(toBeReplaced);

    if(found == string::npos)
        return str;

    string retval = str.substr(0, found);
    retval.append(toReplaceWith);

    while(1)
    {
        const size_t start = found + toBeReplaced.size();
        found = str.find(toBeReplaced, start);

        if(found != string::npos)
        {
            retval.append(str.substr(start, found - start));
            retval.append(toReplaceWith);
        }
        else
        {
            retval.append(str.substr(start));
            break;
        }
    }

    return retval;
}

string r_utils::r_string_utils::to_lower(const string& str)
{
    string retval = str;
    transform(retval.begin(), retval.end(), retval.begin(), 
              [](unsigned char c) -> char { return (char)std::tolower(c); });
    return retval;
}

string r_utils::r_string_utils::to_upper(const string& str)
{
    string retval = str;
    transform(retval.begin(), retval.end(), retval.begin(),
              [](unsigned char c) -> char { return (char)std::toupper(c); });
    return retval;
}

static bool _is_valid_uri_character(char c)
{
    return isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~';
}

string r_utils::r_string_utils::uri_encode(const string& str)
{
    string retval;

    if(!str.empty())
    {
        char c;

        for(size_t i = 0, size = str.size(); i < size; ++i)
        {
            c = str[i];

            if(_is_valid_uri_character(c))
                retval += c;
            else retval += format("%%%02X", c);
        }
    }

    return retval;
}

string r_utils::r_string_utils::uri_decode(const string& str)
{
    string retval;

    if(!str.empty())
    {
        char c;

        for(size_t i = 0, size = str.size(); i < size; ++i)
        {
            c = str[i];

            // spaces
            // The current spec (RFC 3986) does not permit encoding spaces with
            // +, but older versions of the spec do, so we decode + as space but
            // do not encode space as +.
            if(c == '+')
                retval += ' ';
            // unsafe characters
            else if(c == '%')
            {
                // hex to char conversion of next 2 characters
                if(i + 2 < size)
                {
                    //Initial contents of hexStr are irrelevant. It just needs to be the right length.
                    string hexStr = "XX";
                    for(size_t j = 0; j < 2; ++j)
                    {
                        if(isxdigit(str[i+j+1]))
                            hexStr[j] = str[i+j+1];
                        else
                            R_STHROW(r_invalid_argument_exception, ("malformed url"));
                    }

                    unsigned int val = 0;
#ifdef IS_WINDOWS
                    sscanf_s(hexStr.c_str(), "%x", &val);
#endif
#if defined(IS_LINUX) || defined(IS_MACOS)
                    sscanf(hexStr.c_str(), "%x", &val);
#endif
                    retval += r_string_utils::format("%c", (char)val);
                    i += 2;
                }
                else
                    R_STHROW(r_invalid_argument_exception, ("malformed url"));
            }
            else
                retval += c;
        }
    }

    return retval;
}

static const char base64_encoding_table[64] =
{
    'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
    'Q','R','S','T','U','V','W','X','Y','Z','a','b','c','d','e','f',
    'g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v',
    'w','x','y','z','0','1','2','3','4','5','6','7','8','9','+','/'
};

string r_utils::r_string_utils::to_base64( const void* source, size_t length )
{
    size_t srcLen = length;
    const size_t bufferSize = (4 * ((srcLen + 2 - ((srcLen + 2) % 3)) / 3));

    vector<char> destBuffer(bufferSize);

    size_t encodeLen = 0;
    uint8_t* pData = (uint8_t*)&destBuffer[0];
    uint8_t* src = (uint8_t*)source;

    // Encode
    if( (src != nullptr) && (srcLen > 0))
    {
        for(size_t i = 0;  i < (srcLen / 3); i++, src += 3)
        {
            pData[encodeLen++] = base64_encoding_table[src[0]>>2];
            pData[encodeLen++] = base64_encoding_table[((src[0]& 0x3)<<4) | (src[1]>>4)];
            pData[encodeLen++] = base64_encoding_table[((src[1]& 0xF)<<2) | (src[2]>>6)];
            pData[encodeLen++] = base64_encoding_table[(src[2]& 0x3F)];
        }

        // Add padding '=' if necessary
        switch (srcLen % 3)
        {
        case 1:
            pData[encodeLen++] = base64_encoding_table[src[0]>>2];
            pData[encodeLen++] = base64_encoding_table[(src[0]& 0x3)<<4];
            pData[encodeLen++] = '=';
            pData[encodeLen++] = '=';
            break;

        case 2:
            pData[encodeLen++] = base64_encoding_table[src[0]>>2];
            pData[encodeLen++] = base64_encoding_table[((src[0]& 0x3)<<4) | (src[1]>>4)];
            pData[encodeLen++] = base64_encoding_table[((src[1]& 0xF)<<2)];
            pData[encodeLen++] = '=';
            break;
        }
    }
    else
    {
        // no data to encode. return an empty string.
        return string();
    }

    return string(&destBuffer[0], destBuffer.size());
}

enum CHAR_CLASS
{
    LOWER,
    UPPER,
    DIGIT,
    PLUS,
    EQUAL,
    SLASH,
    NEWLINE,
    CR,
    OTHER
};

static CHAR_CLASS _get_char_class(char c)
{
    if (isupper(c))
        return UPPER;
    else if (isdigit(c))
        return DIGIT;
    else if (islower(c))
        return LOWER;
    else if (c == '/')
        return SLASH;
    else if (c == '+')
        return PLUS;
    else if (c == '=')
        return EQUAL;
    else if (c == '\n')
        return NEWLINE;
    else if (c == '\r')
        return CR;
    else
        return OTHER;
}

vector<uint8_t> r_utils::r_string_utils::from_base64(const std::string str)
{
    if (str.size() < 2)
        return vector<uint8_t>();

    // This buffer size is an upper bound.
    // This value can be: N, N+1 or N+2,
    // where N is the length of the raw data.
    size_t bufferSize = ((4 * str.size() / 3) + 3) & ~3;

    // Allocate some memory
    vector<uint8_t> destBuffer(bufferSize);

    uint8_t* pData = &destBuffer[0];
    uint8_t* src = (uint8_t*)str.c_str();
    size_t cursor = 0;
    size_t decodeLen = 0;
    uint8_t byteNumber = 0;
    uint8_t c;
    bool done = false;

    while (cursor < str.size())
    {
        c = *src++;
        cursor++;

        switch (_get_char_class(c))
        {
        case UPPER:
            c = c - 'A';
            break;
        case DIGIT:
            c -= (uint8_t)('0' - 52);
            break;
        case LOWER:
            c -= (uint8_t)('a' - 26);
            break;
        case SLASH:
            c = 63;
            break;
        case PLUS:
            c = 62;
            break;
        case NEWLINE:
            continue;  // Just skip any new lines (Base64 sometimes has \n's)
        case CR:
            continue;  // Just skip any carriage returns (Base64 sometimes has CR's)
        case EQUAL:
            done = true;
            break;
        default:
            // This should never happen. Return an empty object.
            return vector<uint8_t>();
        }

        // If we haven't hit an '=' sign keep going
        if (!done)
        {
            switch(byteNumber++)
            {
            case 0:
                pData[decodeLen] = c << 2;
                break;
            case 1:
                pData[decodeLen++] |= c >> 4;
                pData[decodeLen] = c << 4;
                break;
            case 2:
                pData[decodeLen++] |= c >> 2;
                pData[decodeLen] = c << 6;
                break;
            case 3:
                pData[decodeLen++] |= c;
                byteNumber = 0;
                break;
            default:
                break;
            }
        }
    }

    // Actual raw data was less than our upper bound.
    // Copy the memory into a smaller buffer so we know its actual size.
    if ( decodeLen < destBuffer.size() )
    {
        vector<uint8_t> tempBuffer(decodeLen);
        memcpy(&tempBuffer[0], &destBuffer[0], decodeLen);
        destBuffer = tempBuffer;
    }

    return destBuffer;
}

static bool verify_digit(char c){ return isdigit(c) != 0; }

bool r_utils::r_string_utils::is_integer(const string& str, bool canHaveSign)
{
    const size_t first = str.find_first_not_of(' ');

    if(first == string::npos || (!canHaveSign && str[first] == '-'))
        return false;

    const size_t strippedFront = str[first] == '-' ? first + 1 : first;
    const size_t last = str.find_last_not_of(' ');
    const size_t strippedBack = last == string::npos ? 0 : (str.size() - 1) - last;
    const int numSize = (int)(str.size() - (strippedFront + strippedBack));

    if(numSize == 0)
        return false;

    return count_if(str.begin() + strippedFront,
                    str.end() - strippedBack,
                    verify_digit) == numSize;
}

string r_utils::r_string_utils::lstrip(const string& str)
{
    string retval = str;
    size_t pos = 0;
    while(pos < retval.size() && r_utils::r_string_utils::is_space(retval[pos])) pos++;
    retval.erase(0, pos);
    return retval;
}

string r_utils::r_string_utils::rstrip(const string& str)
{
    string retval = str;
    size_t pos = retval.size();
    while(pos > 0 && r_utils::r_string_utils::is_space(retval[pos - 1])) pos--;
    retval.erase(pos);
    return retval;
}

string r_utils::r_string_utils::strip(const string& str)
{
    auto retval = r_utils::r_string_utils::rstrip(str);
    return r_utils::r_string_utils::lstrip(retval);
}

string r_utils::r_string_utils::strip_eol(const string& str)
{    
    if(r_utils::r_string_utils::ends_with(str, "\r\n"))
        return str.substr(0, str.size() - 2);
    if(r_utils::r_string_utils::ends_with(str, "\n"))
        return str.substr(0, str.size() - 1);
    return str;
}

bool r_utils::r_string_utils::starts_with(const std::string& str, const std::string& other)
{
    const size_t otherSize = other.size();
    return otherSize <= str.size() && str.compare(0, otherSize, other) == 0;
}

bool r_utils::r_string_utils::ends_with(const std::string& str, const std::string& other)
{
    const size_t otherSize = other.size();
    const size_t thisSize = str.size();
    return (otherSize <= thisSize) &&
        (str.compare(thisSize - otherSize, otherSize, other) == 0);
}

int r_utils::r_string_utils::s_to_int(const string& s)
{
    return stoi(s);
}

unsigned int r_utils::r_string_utils::s_to_uint(const std::string& s)
{
    return stoul(s);
}

uint8_t r_utils::r_string_utils::s_to_uint8(const string& s)
{
    return (uint8_t)stoul(s);
}

int8_t r_utils::r_string_utils::s_to_int8(const string& s)
{
    return (int8_t)stoi(s);
}

uint16_t r_utils::r_string_utils::s_to_uint16(const string& s)
{
    return (uint16_t)stoul(s);
}

int16_t r_utils::r_string_utils::s_to_int16(const string& s)
{
    return (int16_t)stoi(s);
}

uint32_t r_utils::r_string_utils::s_to_uint32(const string& s)
{
    return stoul(s);
}

int32_t r_utils::r_string_utils::s_to_int32(const string& s)
{
    return stol(s);
}

uint64_t r_utils::r_string_utils::s_to_uint64(const string& s)
{
    return stoull(s);
}

int64_t r_utils::r_string_utils::s_to_int64(const string& s)
{
    return stoll(s);
}

double r_utils::r_string_utils::s_to_double(const string& s)
{
    return stod(s);
}

float r_utils::r_string_utils::s_to_float(const string& s)
{
    return stof(s);
}

size_t r_utils::r_string_utils::s_to_size_t(const string& s)
{
    return stoul(s);
}

string r_utils::r_string_utils::int_to_s(int val)
{
    return to_string(val);
}

std::string r_utils::r_string_utils::uint_to_s(unsigned int val)
{
    return to_string(val);
}

string r_utils::r_string_utils::int8_to_s(int8_t val)
{
    return to_string(val);
}

string r_utils::r_string_utils::uint8_to_s(uint8_t val)
{
    return to_string(val);
}

string r_utils::r_string_utils::int16_to_s(int16_t val)
{
    return to_string(val);
}

string r_utils::r_string_utils::uint16_to_s(uint16_t val)
{
    return to_string(val);
}

string r_utils::r_string_utils::int32_to_s(int32_t val)
{
    return to_string(val);
}

string r_utils::r_string_utils::uint32_to_s(uint32_t val)
{
    return to_string(val);
}

string r_utils::r_string_utils::int64_to_s(int64_t val)
{
    return to_string(val);
}

string r_utils::r_string_utils::uint64_to_s(uint64_t val)
{
    return to_string(val);
}

string r_utils::r_string_utils::double_to_s(double val, int precision)
{
    std::ostringstream out;
    out.precision(precision);
    out << std::fixed << val;
    return out.str();
}

string r_utils::r_string_utils::float_to_s(float val, int precision)
{
    std::ostringstream out;
    out.precision(precision);
    out << std::fixed << val;
    return out.str();
}

string r_utils::r_string_utils::size_t_to_s(size_t val)
{
    return to_string((uint32_t)val);
}

std::string r_utils::r_string_utils::convert_utf16_string_to_multi_byte_string(const uint16_t* str)
{
    return convert_utf16_string_to_multi_byte_string(str, (size_t)-1);
}

std::string r_utils::r_string_utils::convert_utf16_string_to_multi_byte_string(const uint16_t* str, size_t length)
{
    std::string out;
    if(str == NULL)
        return out;
    unsigned int codepoint = 0;
    for (size_t i = 0; i < length && *str != 0; ++i, ++str)
    {
        if(*str >= 0xd800 && *str <= 0xdbff)
            codepoint = ((*str - 0xd800) << 10) + 0x10000;
        else
        {
            if(*str >= 0xdc00 && *str <= 0xdfff)
                codepoint |= *str - 0xdc00;
            else
                codepoint = *str;

            if(codepoint <= 0x7f)
                out.append(1, static_cast<char>(codepoint));
            else if(codepoint <= 0x7ff)
            {
                out.append(1, static_cast<char>(0xc0 | ((codepoint >> 6) & 0x1f)));
                out.append(1, static_cast<char>(0x80 | (codepoint & 0x3f)));
            }
            else if(codepoint <= 0xffff)
            {
                out.append(1, static_cast<char>(0xe0 | ((codepoint >> 12) & 0x0f)));
                out.append(1, static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
                out.append(1, static_cast<char>(0x80 | (codepoint & 0x3f)));
            }
            else
            {
                out.append(1, static_cast<char>(0xf0 | ((codepoint >> 18) & 0x07)));
                out.append(1, static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
                out.append(1, static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
                out.append(1, static_cast<char>(0x80 | (codepoint & 0x3f)));
            }
            codepoint = 0;
        }
    }
    return out;
}
std::vector<uint16_t> r_utils::r_string_utils::convert_multi_byte_string_to_utf16_string(const std::string& str)
{
    std::vector<uint16_t> out;
    if(str.empty())
        return out;
    char* place = const_cast<char*>(str.c_str());
    unsigned int codepoint = 0;
    int following = 0;
    for (;  *place != 0;  ++place)
    {
        unsigned char ch = *place;
        if(ch <= 0x7f)
        {
            codepoint = ch;
            following = 0;
        }
        else if(ch <= 0xbf)
        {
            if(following > 0)
            {
                codepoint = (codepoint << 6) | (ch & 0x3f);
                --following;
            }
        }
        else if(ch <= 0xdf)
        {
            codepoint = ch & 0x1f;
            following = 1;
        }
        else if(ch <= 0xef)
        {
            codepoint = ch & 0x0f;
            following = 2;
        }
        else
        {
            codepoint = ch & 0x07;
            following = 3;
        }
        if(following == 0)
        {
            if(codepoint > 0xffff)
            {
                out.push_back(static_cast<wchar_t>(0xd800 + (codepoint >> 10)));
                out.push_back(static_cast<wchar_t>(0xdc00 + (codepoint & 0x03ff)));
            }
            else
                out.push_back(static_cast<wchar_t>(codepoint));
            codepoint = 0;
        }
    }
    return out;
}

std::string r_utils::r_string_utils::convert_utf32_string_to_multi_byte_string(const uint32_t* str)
{
    return convert_utf32_string_to_multi_byte_string(str, (size_t)-1);
}

std::string r_utils::r_string_utils::convert_utf32_string_to_multi_byte_string(const uint32_t* str, size_t length)
{
    std::string out;
    if(str == NULL)
        return out;

    size_t i = 0;
    for (wchar_t* temp = (wchar_t*)str; i < length && *temp != 0; ++temp, ++i)
    {
        unsigned int codepoint = *temp;

        if(codepoint <= 0x7f)
            out.append(1, static_cast<char>(codepoint));
        else if(codepoint <= 0x7ff)
        {
            out.append(1, static_cast<char>(0xc0 | ((codepoint >> 6) & 0x1f)));
            out.append(1, static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
        else if(codepoint <= 0xffff)
        {
            out.append(1, static_cast<char>(0xe0 | ((codepoint >> 12) & 0x0f)));
            out.append(1, static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            out.append(1, static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
      else
        {
            out.append(1, static_cast<char>(0xf0 | ((codepoint >> 18) & 0x07)));
            out.append(1, static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
            out.append(1, static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            out.append(1, static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
    }
    return out;
}

std::vector<uint32_t> r_utils::r_string_utils::convert_multi_byte_string_to_utf32_string(const std::string& str)
{
    std::vector<uint32_t> out;

    wchar_t codepoint = 0;
    int following = 0;
    for (char* temp = const_cast<char*>(str.c_str());  *temp != 0;  ++temp)
    {
        unsigned char ch = *temp;
        if(ch <= 0x7f)
        {
            codepoint = ch;
            following = 0;
        }
        else if(ch <= 0xbf)
        {
            if(following > 0)
            {
                codepoint = (codepoint << 6) | (ch & 0x3f);
                --following;
            }
        }
        else if(ch <= 0xdf)
        {
            codepoint = ch & 0x1f;
            following = 1;
        }
        else if(ch <= 0xef)
        {
            codepoint = ch & 0x0f;
            following = 2;
        }
        else
        {
            codepoint = ch & 0x07;
            following = 3;
        }
        if(following == 0)
        {
            out.push_back(codepoint);
            codepoint = 0;
        }
    }
    return out;
}

std::string r_utils::r_string_utils::convert_wide_string_to_multi_byte_string(const wchar_t* str)
{
#ifdef IS_WINDOWS
    std::string result(convert_utf16_string_to_multi_byte_string((uint16_t*)str));
#else
    std::string result(convert_utf32_string_to_multi_byte_string((uint32_t*)str));
#endif
    return result;
}

std::string r_utils::r_string_utils::convert_wide_string_to_multi_byte_string(const wchar_t* str, size_t length)
{
#ifdef IS_WINDOWS
    std::string result(convert_utf16_string_to_multi_byte_string((uint16_t*)str, length));
#else
    std::string result(convert_utf32_string_to_multi_byte_string((uint32_t*)str, length));
#endif
    return result;
}

std::wstring r_utils::r_string_utils::convert_multi_byte_string_to_wide_string(const std::string& str)
{
#ifdef IS_WINDOWS
    std::vector<uint16_t> converted = convert_multi_byte_string_to_utf16_string(str);
    std::wstring result(converted.begin(),converted.end());
#else
    std::vector<uint32_t> converted = convert_multi_byte_string_to_utf32_string(str);
    std::wstring result(converted.begin(),converted.end());
#endif
    return result;
}
