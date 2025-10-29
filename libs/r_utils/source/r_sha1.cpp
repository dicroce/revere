
#include "r_utils/r_sha1.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_exception.h"
#include <string.h>
#include <array>

using namespace r_utils;
using namespace std;

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

#ifdef IS_LITTLE_ENDIAN
#define blk0(i) (block->l[i] = (rol(block->l[i],24)&0xFF00FF00) | (rol(block->l[i],8)&0x00FF00FF))
#elif IS_BIG_ENDIAN
#define blk0(i) block->l[i]
#else
#error "Endianness not defined!"
#endif
#define blk(i) (block->l[i&15] = rol(block->l[(i+13)&15]^block->l[(i+8)&15] ^block->l[(i+2)&15]^block->l[i&15],1))

#define R0(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk0(i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R1(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk(i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R2(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0x6ED9EBA1+rol(v,5);w=rol(w,30);
#define R3(v,w,x,y,z,i) z+=(((w|x)&y)|(w&x))+blk(i)+0x8F1BBCDC+rol(v,5);w=rol(w,30);
#define R4(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0xCA62C1D6+rol(v,5);w=rol(w,30);

r_sha1::r_sha1() :
    _state({0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0}),
    _count({0, 0}),
    _buffer({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
             0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
             0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
             0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0})
{
}

r_sha1::~r_sha1()
{
}

void r_sha1::update(const uint8_t* src, size_t size)
{
    uint32_t i, j;

    j = _count[0];
    if ((_count[0] += ((uint32_t)size) << 3) < j)
        _count[1]++;

    _count[1] += ((uint32_t)size >> 29);
    j = (j >> 3) & 63;

    if((j + size) > 63)
    {
        memcpy(&_buffer[j], src, (i = 64 - j));

        _transform(&_state[0], &_buffer[0]);

        for (; i + 63 < size; i += 64)
            _transform(&_state[0], &src[i]);

        j = 0;
    }
    else
        i = 0;

    memcpy(&_buffer[j], &src[i], size - i);
}

void r_sha1::finalize()
{
    array<uint8_t, 8> finalcount;
    for(size_t i = 0; i < 8; i++)
        finalcount[i] = (uint8_t) ((_count[(i >= 4 ? 0 : 1)] >> ((3 - (i & 3)) * 8)) & 255);

    uint8_t c = 0200;

    update(&c, 1);
    while((_count[0] & 504) != 448)
    {
        c = 0000;
        update(&c, 1);
    }

    update(&finalcount[0], 8);
    for(size_t i = 0; i < 20; i++)
        _result[i] = (uint8_t)((_state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);

    _finalized = true;
}

void r_sha1::get(uint8_t* output)
{
    if(!_finalized)
        R_THROW(("Please finalize() sha1 before get()."));

    memcpy(output, &_result[0], 20);
}

string r_sha1::get_as_string()
{
    if(!_finalized)
        R_THROW(("Please finalize() sha1 before get_as_string()."));

    return r_string_utils::format(
        "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
        _result[0], _result[1],_result[2], _result[3],
        _result[4], _result[5], _result[6], _result[7],
        _result[8], _result[9], _result[10], _result[11],
        _result[12], _result[13], _result[14], _result[15],
        _result[16], _result[17], _result[18], _result[19]
    );
}

void r_sha1::_transform(uint32_t* state, const uint8_t* buffer)
{
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];

    typedef union
    {
        uint8_t c[64];
        uint32_t l[16];
    } SHA1_DATA_VIEW;

    SHA1_DATA_VIEW block[1]; // use array to appear as a pointer

    memcpy(block, &buffer[0], 64);

    R0(a, b, c, d, e, 0);
    R0(e, a, b, c, d, 1);
    R0(d, e, a, b, c, 2);
    R0(c, d, e, a, b, 3);
    R0(b, c, d, e, a, 4);
    R0(a, b, c, d, e, 5);
    R0(e, a, b, c, d, 6);
    R0(d, e, a, b, c, 7);
    R0(c, d, e, a, b, 8);
    R0(b, c, d, e, a, 9);
    R0(a, b, c, d, e, 10);
    R0(e, a, b, c, d, 11);
    R0(d, e, a, b, c, 12);
    R0(c, d, e, a, b, 13);
    R0(b, c, d, e, a, 14);
    R0(a, b, c, d, e, 15);
    R1(e, a, b, c, d, 16);
    R1(d, e, a, b, c, 17);
    R1(c, d, e, a, b, 18);
    R1(b, c, d, e, a, 19);
    R2(a, b, c, d, e, 20);
    R2(e, a, b, c, d, 21);
    R2(d, e, a, b, c, 22);
    R2(c, d, e, a, b, 23);
    R2(b, c, d, e, a, 24);
    R2(a, b, c, d, e, 25);
    R2(e, a, b, c, d, 26);
    R2(d, e, a, b, c, 27);
    R2(c, d, e, a, b, 28);
    R2(b, c, d, e, a, 29);
    R2(a, b, c, d, e, 30);
    R2(e, a, b, c, d, 31);
    R2(d, e, a, b, c, 32);
    R2(c, d, e, a, b, 33);
    R2(b, c, d, e, a, 34);
    R2(a, b, c, d, e, 35);
    R2(e, a, b, c, d, 36);
    R2(d, e, a, b, c, 37);
    R2(c, d, e, a, b, 38);
    R2(b, c, d, e, a, 39);
    R3(a, b, c, d, e, 40);
    R3(e, a, b, c, d, 41);
    R3(d, e, a, b, c, 42);
    R3(c, d, e, a, b, 43);
    R3(b, c, d, e, a, 44);
    R3(a, b, c, d, e, 45);
    R3(e, a, b, c, d, 46);
    R3(d, e, a, b, c, 47);
    R3(c, d, e, a, b, 48);
    R3(b, c, d, e, a, 49);
    R3(a, b, c, d, e, 50);
    R3(e, a, b, c, d, 51);
    R3(d, e, a, b, c, 52);
    R3(c, d, e, a, b, 53);
    R3(b, c, d, e, a, 54);
    R3(a, b, c, d, e, 55);
    R3(e, a, b, c, d, 56);
    R3(d, e, a, b, c, 57);
    R3(c, d, e, a, b, 58);
    R3(b, c, d, e, a, 59);
    R4(a, b, c, d, e, 60);
    R4(e, a, b, c, d, 61);
    R4(d, e, a, b, c, 62);
    R4(c, d, e, a, b, 63);
    R4(b, c, d, e, a, 64);
    R4(a, b, c, d, e, 65);
    R4(e, a, b, c, d, 66);
    R4(d, e, a, b, c, 67);
    R4(c, d, e, a, b, 68);
    R4(b, c, d, e, a, 69);
    R4(a, b, c, d, e, 70);
    R4(e, a, b, c, d, 71);
    R4(d, e, a, b, c, 72);
    R4(c, d, e, a, b, 73);
    R4(b, c, d, e, a, 74);
    R4(a, b, c, d, e, 75);
    R4(e, a, b, c, d, 76);
    R4(d, e, a, b, c, 77);
    R4(c, d, e, a, b, 78);
    R4(b, c, d, e, a, 79);

    /* Add the working vars back into context.state[] */
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;

    /* Wipe variables */
    a = b = c = d = e = 0;
    memset(block, 0, sizeof(block));
}
