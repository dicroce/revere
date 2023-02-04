
#ifndef _r_utils_r_blob_tree_h
#define _r_utils_r_blob_tree_h

#include "r_utils/r_exception.h"

#include <map>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <sstream>

class test_r_utils_r_blob_tree;

namespace r_utils
{

class r_blob_tree
{
    friend class ::test_r_utils_r_blob_tree;

public:
    enum node_type
    {
        NT_OBJECT,
        NT_ARRAY,
        NT_LEAF
    };

    R_API static std::vector<uint8_t> serialize(const r_blob_tree& rt, uint32_t version);

    R_API static r_blob_tree deserialize(const uint8_t* p, size_t size, uint32_t& version);

    R_API r_blob_tree& operator[](const std::string& key)
    {
        if(!_childrenByIndex.empty())
            R_STHROW(r_internal_exception, ("r_blob_tree node cannot be both an array and an object."));
        return _children[key];
    }

    R_API const r_blob_tree& at(const std::string& key) const
    {
        if(!_childrenByIndex.empty())
            R_STHROW(r_internal_exception, ("r_blob_tree node cannot be both an array and an object."));
        return _children.at(key);
    }

    R_API bool has_key(const std::string& key) const
    {
        if(!_childrenByIndex.empty())
            R_STHROW(r_internal_exception, ("r_blob_tree node cannot be both an array and an object."));
        return _children.find(key) != _children.end();
    }

    R_API r_blob_tree& operator[](size_t index)
    {
        if(!_children.empty())
            R_STHROW(r_internal_exception, ("r_blob_tree node cannot be both an object and an array."));
        if(_childrenByIndex.size() < (index+1))
            _childrenByIndex.resize(index+1);
        return _childrenByIndex[index];
    }

    R_API const r_blob_tree& at(size_t index) const
    {
        if(!_children.empty())
            R_STHROW(r_internal_exception, ("r_blob_tree node cannot be both an object and an array."));
        return _childrenByIndex.at(index);
    }

    R_API bool has_index(size_t index) const
    {
        if(!_children.empty())
            R_STHROW(r_internal_exception, ("r_blob_tree node cannot be both an object and an array."));
        return _childrenByIndex.size() > index;
    }

    R_API size_t size() const
    {
        if(!_children.empty())
            R_STHROW(r_internal_exception, ("r_blob_tree node cannot be both an object and an array."));
        return _childrenByIndex.size();
    }

    R_API r_blob_tree& operator=(const std::string& value)
    {
        _payload_storage.resize(value.length());
        if(!_payload_storage.empty())
            memcpy(&_payload_storage[0], value.c_str(), value.length());

        return *this;
    }

    R_API r_blob_tree& operator=(const std::vector<uint8_t>& payload_storage)
    {
        _payload_storage = payload_storage;

        return *this;
    }

    R_API inline std::vector<uint8_t> get() const
    {
        return _payload_storage;
    }

    R_API inline std::string get_string() const
    {
        if(_payload_storage.empty())
            R_THROW(("Unable to fetch empty value."));

        return std::string((char*)&_payload_storage[0], _payload_storage.size());
    }

    template<typename T>
    R_API inline T get_value() const
    {
        std::stringstream convert(get_string());
        T value;
        convert >> value;
        return value;
    }

private:
    static size_t _sizeof_treeb(const r_blob_tree& rt);
    static size_t _write_treeb(const r_blob_tree& rt, uint8_t* p, uint8_t* end);
    static size_t _read_treeb(const uint8_t* p, const uint8_t* end, r_blob_tree& rt);
    inline static size_t _bytes_left(const uint8_t* p, const uint8_t* end) { return end - p; }

    std::map<std::string, r_blob_tree> _children;
    std::vector<r_blob_tree> _childrenByIndex;
    std::vector<uint8_t> _payload_storage;
};

}

#endif