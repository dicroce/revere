#include "r_utils/r_blob_tree.h"
#include "r_utils/r_socket.h"   // for r_networking helpers
#include <cstring>
#include <algorithm>

using namespace r_utils;
using namespace std;

namespace
{
    constexpr uint32_t RBT_MAGIC = 0x52425430; // "RBT0"

    inline void write_u32(uint8_t*& p, uint32_t v)
    {
        uint32_t net = r_networking::r_htonl(v);
        std::memcpy(p, &net, sizeof(net));
        p += sizeof(net);
    }

    inline uint32_t read_u32(const uint8_t*& p)
    {
        uint32_t net; std::memcpy(&net, p, sizeof(net)); p += sizeof(net);
        return r_networking::r_ntohl(net);
    }

    inline void write_u16(uint8_t*& p, uint16_t v)
    {
        uint16_t net = r_networking::r_htons(v);
        std::memcpy(p, &net, sizeof(net));
        p += sizeof(net);
    }

    inline uint16_t read_u16(const uint8_t*& p)
    {
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
    uint32_t magic = read_u32(p);
    if(magic != RBT_MAGIC)
        R_STHROW(r_invalid_argument_exception,("r_blob_tree bad magic"));
    version = read_u32(p);
    r_blob_tree root;
    _read_treeb(p, end, root);
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
size_t r_blob_tree::_read_treeb(const uint8_t* p, const uint8_t* end, r_blob_tree& rt)
{
    const uint8_t* start = p;
    if(_bytes_left(p,end) < 1) R_STHROW(r_invalid_argument_exception,("buffer too small"));
    rt._type = static_cast<node_type>(*p++);

    if(rt._type == NT_OBJECT)
    {
        uint32_t cnt = read_u32(p);
        for(uint32_t i=0;i<cnt;++i)
        {
            uint16_t klen = read_u16(p);
            if(_bytes_left(p,end) < klen) R_STHROW(r_invalid_argument_exception,("bad key length"));
            std::string key(reinterpret_cast<const char*>(p), klen);
            p += klen;
            r_blob_tree child;
            p += _read_treeb(p,end,child);
            rt._children.emplace(std::move(key), std::move(child));
        }
    }
    else if(rt._type == NT_ARRAY)
    {
        uint32_t cnt = read_u32(p);
        rt._childrenByIndex.resize(cnt);
        for(uint32_t i=0;i<cnt;++i)
            p += _read_treeb(p,end,rt._childrenByIndex[i]);
    }
    else // NT_LEAF
    {
        uint32_t len = read_u32(p);
        if(_bytes_left(p,end) < len) R_STHROW(r_invalid_argument_exception,("payload overrun"));
        rt._payload_storage.assign(p, p + len);
        p += len;
    }
    return static_cast<size_t>(p - start);
}
