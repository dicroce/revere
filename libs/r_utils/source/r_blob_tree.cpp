#include "r_utils/r_blob_tree.h"
#include "r_utils/r_socket.h"   // for r_networking helpers
#include <cstring>
#include <algorithm>

using namespace r_utils;
using namespace std;

namespace
{
    constexpr uint32_t RBT_MAGIC = 0x52425430; // "RBT0"

    // Maximum nesting depth accepted by the reader. Deserialisation recurses per
    // nested object/array; an attacker can request one level per ~5 bytes of
    // input, so without a cap a modest payload blows the stack. 64 is far deeper
    // than any real blob tree the product produces.
    constexpr unsigned RBT_MAX_DEPTH = 64;

    // Throw unless at least n bytes remain in [p, end). Using a signed pointer
    // difference here (rather than the unsigned _bytes_left) means an already
    // overshot p can't wrap the comparison.
    inline void require_bytes(const uint8_t* p, const uint8_t* end, size_t n)
    {
        if(p > end || static_cast<size_t>(end - p) < n)
            R_STHROW(r_utils::r_invalid_argument_exception, ("r_blob_tree: truncated input"));
    }

    inline void write_u32(uint8_t*& p, uint32_t v)
    {
        uint32_t net = r_networking::r_htonl(v);
        std::memcpy(p, &net, sizeof(net));
        p += sizeof(net);
    }

    // Bounds-checked: the read functions verify space before touching memory so
    // a truncated field can never over-read or push p past end.
    inline uint32_t read_u32(const uint8_t*& p, const uint8_t* end)
    {
        require_bytes(p, end, sizeof(uint32_t));
        uint32_t net; std::memcpy(&net, p, sizeof(net)); p += sizeof(net);
        return r_networking::r_ntohl(net);
    }

    inline void write_u16(uint8_t*& p, uint16_t v)
    {
        uint16_t net = r_networking::r_htons(v);
        std::memcpy(p, &net, sizeof(net));
        p += sizeof(net);
    }

    inline uint16_t read_u16(const uint8_t*& p, const uint8_t* end)
    {
        require_bytes(p, end, sizeof(uint16_t));
        uint16_t net; std::memcpy(&net, p, sizeof(net)); p += sizeof(net);
        return r_networking::r_ntohs(net);
    }
}

// ------------------------------------------------------------
// Public API
// ------------------------------------------------------------
vector<uint8_t> r_blob_tree::serialize(const r_blob_tree& rt, uint32_t version)
{
    const size_t total = sizeof(uint32_t) /*magic*/ + sizeof(uint32_t) /*ver*/ + _sizeof_treeb(rt);
    vector<uint8_t> buf(total);
    uint8_t* p = buf.data();
    write_u32(p, RBT_MAGIC);
    write_u32(p, version);
    _write_treeb(rt, p, buf.data() + buf.size());
    return buf;
}

r_blob_tree r_blob_tree::deserialize(const uint8_t* p, size_t size, uint32_t& version)
{
    const uint8_t* end = p + size;
    if(size < 8)
        R_STHROW(r_invalid_argument_exception,("r_blob_tree buffer too small"));
    uint32_t magic = read_u32(p, end);
    if(magic != RBT_MAGIC)
        R_STHROW(r_invalid_argument_exception,("r_blob_tree bad magic"));
    version = read_u32(p, end);
    r_blob_tree root;
    _read_treeb(p, end, root, 0);
    return root;
}

// ------------------------------------------------------------
// Size pass
// ------------------------------------------------------------
size_t r_blob_tree::_sizeof_treeb(const r_blob_tree& rt)
{
    size_t sz = 1; // node_type
    switch(rt._type)
    {
        case NT_OBJECT:
            sz += 4;
            for(const auto& [k,v] : rt._children)
                sz += 2 + k.size() + _sizeof_treeb(v);
            break;
        case NT_ARRAY:
            sz += 4;
            for(const auto& n : rt._childrenByIndex)
                sz += _sizeof_treeb(n);
            break;
        case NT_LEAF:
            sz += 4 + rt._payload_storage.size();
            break;
    }
    return sz;
}

// ------------------------------------------------------------
// Write pass
// ------------------------------------------------------------
size_t r_blob_tree::_write_treeb(const r_blob_tree& rt, uint8_t* p, uint8_t* end)
{
    uint8_t* start = p;
    if(_bytes_left(p,end) < 1) R_STHROW(r_invalid_argument_exception,("buffer too small"));
    *p++ = static_cast<uint8_t>(rt._type);

    if(rt._type == NT_OBJECT)
    {
        write_u32(p, static_cast<uint32_t>(rt._children.size()));
        for(const auto& [k,v] : rt._children)
        {
            write_u16(p, static_cast<uint16_t>(k.size()));
            std::memcpy(p, k.data(), k.size()); p += k.size();
            p += _write_treeb(v, p, end);
        }
    }
    else if(rt._type == NT_ARRAY)
    {
        write_u32(p, static_cast<uint32_t>(rt._childrenByIndex.size()));
        for(const auto& n : rt._childrenByIndex)
            p += _write_treeb(n, p, end);
    }
    else // NT_LEAF
    {
        write_u32(p, static_cast<uint32_t>(rt._payload_storage.size()));
        std::memcpy(p, rt._payload_storage.data(), rt._payload_storage.size());
        p += rt._payload_storage.size();
    }
    return static_cast<size_t>(p - start);
}

// ------------------------------------------------------------
// Read pass
// ------------------------------------------------------------
size_t r_blob_tree::_read_treeb(const uint8_t* p, const uint8_t* end, r_blob_tree& rt, unsigned depth)
{
    if(depth > RBT_MAX_DEPTH)
        R_STHROW(r_invalid_argument_exception,("r_blob_tree: max nesting depth exceeded"));

    const uint8_t* start = p;
    if(_bytes_left(p,end) < 1) R_STHROW(r_invalid_argument_exception,("buffer too small"));

    // Validate the type tag before trusting it — a byte outside the enum is
    // malformed input, not a silently-ignored node.
    uint8_t type_byte = *p++;
    if(type_byte > NT_LEAF)
        R_STHROW(r_invalid_argument_exception,("r_blob_tree: invalid node type"));
    rt._type = static_cast<node_type>(type_byte);

    if(rt._type == NT_OBJECT)
    {
        uint32_t cnt = read_u32(p, end);
        // Each entry needs at least a 2-byte key length + a 1-byte child type, so
        // a count larger than the bytes remaining is impossible — reject early
        // rather than spin the loop against a bogus count.
        if(cnt > _bytes_left(p, end) / 3)
            R_STHROW(r_invalid_argument_exception,("r_blob_tree: object count exceeds input"));
        for(uint32_t i=0;i<cnt;++i)
        {
            uint16_t klen = read_u16(p, end);
            if(_bytes_left(p,end) < klen) R_STHROW(r_invalid_argument_exception,("bad key length"));
            std::string key(reinterpret_cast<const char*>(p), klen);
            p += klen;
            r_blob_tree child;
            p += _read_treeb(p,end,child,depth+1);
            rt._children.emplace(std::move(key), std::move(child));
        }
    }
    else if(rt._type == NT_ARRAY)
    {
        uint32_t cnt = read_u32(p, end);
        // Each element is at least a 1-byte type tag, so an element count larger
        // than the bytes remaining is malformed. This also bounds the resize()
        // below to the input size, preventing an allocation bomb from a tiny
        // message with a 4-billion element count.
        if(cnt > _bytes_left(p, end))
            R_STHROW(r_invalid_argument_exception,("r_blob_tree: array count exceeds input"));
        rt._childrenByIndex.resize(cnt);
        for(uint32_t i=0;i<cnt;++i)
            p += _read_treeb(p,end,rt._childrenByIndex[i],depth+1);
    }
    else // NT_LEAF
    {
        uint32_t len = read_u32(p, end);
        if(_bytes_left(p,end) < len) R_STHROW(r_invalid_argument_exception,("payload overrun"));
        rt._payload_storage.assign(p, p + len);
        p += len;
    }
    return static_cast<size_t>(p - start);
}
