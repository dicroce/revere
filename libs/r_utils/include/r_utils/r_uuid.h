
#ifndef r_utils_r_uuid_h
#define r_utils_r_uuid_h

#include "r_utils/r_macro.h"
#include <string>
#include <vector>
#include <cstdint>
#ifdef IS_LINUX
#include <uuid/uuid.h>
#endif
#ifdef IS_MACOS
#include <uuid/uuid.h>
#endif
#ifdef IS_WINDOWS
#include <Rpc.h>
#endif
namespace r_utils
{

namespace r_uuid
{

R_API void generate(uint8_t* uuid);
R_API std::string generate();
R_API std::string uuid_to_s(const uint8_t* uuid);
R_API void s_to_uuid(const std::string& uuidS, uint8_t* uuid);

}

}

#endif
