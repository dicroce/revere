
#include "r_utils/r_uuid.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_string_utils.h"
#include <string.h>

using namespace r_utils;
using namespace std;

void r_utils::r_uuid::generate(uint8_t* uuid)
{
#ifdef IS_LINUX
    uuid_generate_random(uuid);
#endif
#ifdef IS_WINDOWS
    RPC_WSTR str = NULL;
    GUID pguid;
    if(CoCreateGuid(&pguid) != S_OK)
        R_STHROW(r_internal_exception, ("Unable to create uuid."));
    memcpy(uuid, &pguid, 16);
#endif
}

string r_utils::r_uuid::generate()
{
    uint8_t uuid[16];
    generate(&uuid[0]);
    return uuid_to_s(&uuid[0]);
}

string r_utils::r_uuid::uuid_to_s(const uint8_t* uuid)
{
#ifdef IS_LINUX
    char str[69];
    uuid_unparse(uuid, str);
    return str;
#endif
#ifdef IS_WINDOWS
    RPC_CSTR str = NULL;
    if(UuidToStringA((UUID*)uuid, &str) != RPC_S_OK)
        R_STHROW(r_internal_exception, ("Unable to create uuid string."));
    string result = (char*)str;
    //auto result = r_string_utils::convert_utf16_string_to_multi_byte_string((uint16_t*)&str[0]);
    RpcStringFree(&str);
    return result;
#endif
}

void r_utils::r_uuid::s_to_uuid(const string& uuidS, uint8_t* uuid)
{
#ifdef IS_LINUX
    if(uuid_parse(uuidS.c_str(), uuid) != 0)
        R_STHROW(r_invalid_argument_exception, ("Unable to parse uuid string."));
#endif
#ifdef IS_WINDOWS
    GUID pguid;
    if(UuidFromStringA((RPC_CSTR)uuidS.c_str(), &pguid) != RPC_S_OK)
        R_STHROW(r_internal_exception, ("Unable to create uuid from string."));
    memcpy(uuid, &pguid, 16);
#endif
}
