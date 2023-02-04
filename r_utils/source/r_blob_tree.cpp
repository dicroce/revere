
#include "r_utils/r_blob_tree.h"
#include "r_utils/r_socket.h"

using namespace r_utils;
using namespace std;

vector<uint8_t> r_blob_tree::serialize(const r_blob_tree& rt, uint32_t version)
{
    auto size = sizeof(uint32_t) + _sizeof_treeb(rt);
    vector<uint8_t> buffer(size);
    uint8_t* p = &buffer[0];
    uint32_t word = r_networking::r_htonl(version);
    *(uint32_t*)p = word;
    p+=sizeof(uint32_t);
    // &buffer[0] + size is our sentinel (1 past the end)
    _write_treeb(rt, p, &buffer[0] + size);
    return buffer;
}

r_blob_tree r_blob_tree::deserialize(const uint8_t* p, size_t size, uint32_t& version)
{
    auto end = p + size;
    uint32_t word = *(uint32_t*)p;
    p+=sizeof(uint32_t);
    version = r_networking::r_ntohl(word);
    r_blob_tree obj; 
    _read_treeb(p, end, obj);
    return obj;
}

size_t r_blob_tree::_sizeof_treeb(const r_blob_tree& rt)
{
    size_t sum = 1 + sizeof(uint32_t); // type & num children

    if(!rt._children.empty())
    {
        for(auto& cp : rt._children)
            sum += sizeof(uint16_t) + cp.first.length() + _sizeof_treeb(cp.second);
    }
    else if(!rt._childrenByIndex.empty())
    {
        for(auto& c : rt._childrenByIndex)
            sum += _sizeof_treeb(c);
    }
    else sum += sizeof(uint32_t) + rt._payload_storage.size();

    return sum;
}

size_t r_blob_tree::_write_treeb(const r_blob_tree& rt, uint8_t* p, uint8_t* end)
{
    uint8_t* fp = p;

    if(_bytes_left(p, end) < 5)
        R_STHROW(r_invalid_argument_exception, ("Buffer too small to serialize r_blob_tree."));


    uint8_t type = (!rt._children.empty())?NT_OBJECT:(!rt._childrenByIndex.empty())?NT_ARRAY:NT_LEAF;
    *p = type;
    ++p;

    if(type == NT_OBJECT || type == NT_ARRAY)
    {
        uint32_t numChildren = (type==NT_OBJECT)?(uint32_t)rt._children.size():(uint32_t)rt._childrenByIndex.size();
        uint32_t word = r_networking::r_htonl(numChildren);
        *(uint32_t*)p = word;
        p+=sizeof(uint32_t);

        if(type==NT_OBJECT)
        {
            for(auto& cp : rt._children)
            {
                if(_bytes_left(p, end) < sizeof(uint16_t))
                    R_STHROW(r_invalid_argument_exception, ("Buffer too small to serialize r_blob_tree."));
                
                uint16_t nameSize = (uint16_t)cp.first.length();
                uint16_t shortVal = r_networking::r_htons(nameSize);
                *(uint16_t*)p = shortVal;
                p+=sizeof(uint16_t);

                if(_bytes_left(p, end) < nameSize)
                    R_STHROW(r_invalid_argument_exception, ("Buffer too small to serialize r_blob_tree."));            

                memcpy(p, cp.first.c_str(), nameSize);
                p+=nameSize;

                p+=_write_treeb(cp.second, p, end);
            }
        }
        else
        {
            for(auto& c : rt._childrenByIndex)
                p+=_write_treeb(c, p, end);
        }
    }
    else
    {
        if(_bytes_left(p, end) < sizeof(uint32_t))
            R_STHROW(r_invalid_argument_exception, ("Buffer too small to serialize r_blob_tree."));            

        uint32_t payloadSize = (uint32_t)rt._payload_storage.size();
        uint32_t word = r_networking::r_htonl(payloadSize);
        *(uint32_t*)p = word;
        p+=sizeof(uint32_t);

        if(_bytes_left(p, end) < payloadSize)
            R_STHROW(r_invalid_argument_exception, ("Buffer too small to serialize r_blob_tree."));            

        memcpy(p, rt._payload_storage.data(), payloadSize);
        p+=payloadSize;
    }

    return p - fp;
}

size_t r_blob_tree::_read_treeb(const uint8_t* p, const uint8_t* end, r_blob_tree& rt)
{
    if(_bytes_left(p, end) < 5)
        R_STHROW(r_invalid_argument_exception, ("Buffer too small to deserialize r_blob_tree."));            

    const uint8_t* fp = p;
    uint8_t type = *p;
    ++p;

    if(type == NT_OBJECT || type == NT_ARRAY)
    {
        uint32_t word = *(uint32_t*)p;
        p+=sizeof(uint32_t);
        uint32_t numChildren = r_networking::r_ntohl(word);

        if(type == NT_OBJECT)
        {
            for(size_t i = 0; i < numChildren; ++i)
            {
                if(_bytes_left(p, end) < sizeof(uint16_t))
                    R_STHROW(r_invalid_argument_exception, ("Buffer too small to deserialize r_blob_tree."));            

                uint16_t shortVal = *(uint16_t*)p;
                p+=sizeof(uint16_t);
                uint16_t nameLen = r_networking::r_ntohs(shortVal);

                if(_bytes_left(p, end) < nameLen)
                    R_STHROW(r_invalid_argument_exception, ("Buffer too small to deserialize r_blob_tree."));            

                string name((char*)p, nameLen);
                p+=nameLen;
                r_blob_tree childObj;
                p+=_read_treeb(p, end, childObj);
                rt._children[name] = childObj;
            }
        }
        else
        {
            rt._childrenByIndex.resize(numChildren);
            for(size_t i = 0; i < numChildren; ++i)
                p+=_read_treeb(p, end, rt._childrenByIndex[i]);
        }
    }
    else
    {
        if(_bytes_left(p, end) < sizeof(uint32_t))
            R_STHROW(r_invalid_argument_exception, ("Buffer too small to deserialize r_blob_tree."));            

        uint32_t word = *(uint32_t*)p;
        p+=sizeof(uint32_t);
        uint32_t payloadSize = r_networking::r_ntohl(word);

        if(_bytes_left(p, end) < payloadSize)
            R_STHROW(r_invalid_argument_exception, ("Buffer too small to deserialize r_blob_tree."));

        rt._payload_storage.resize(payloadSize);
        memcpy(rt._payload_storage.data(), p, payloadSize);
        p+=payloadSize;
    }

    return p - fp;
}
