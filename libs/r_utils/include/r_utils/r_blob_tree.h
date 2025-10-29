#ifndef _r_utils_r_blob_tree_h
#define _r_utils_r_blob_tree_h

#include "r_utils/r_exception.h"
#include <map>
#include <string>
#include <vector>
#include <cstdint>
#include <sstream>

class test_r_utils_r_blob_tree;

namespace r_utils
{

class r_blob_tree
{
    friend class ::test_r_utils_r_blob_tree;

public:
    enum node_type : uint8_t
    {
        NT_OBJECT = 0,
        NT_ARRAY  = 1,
        NT_LEAF   = 2
    };

    r_blob_tree() = default;

    // ------------ (de)serialisation ------------
    R_API static std::vector<uint8_t> serialize(const r_blob_tree& rt, uint32_t version);
    R_API static r_blob_tree           deserialize(const uint8_t* p, size_t size, uint32_t& version);

    // ------------ object access ------------
    r_blob_tree& operator[](const std::string& key);
    const r_blob_tree& at(const std::string& key) const;
    bool has_key(const std::string& key) const;

    // ------------ array access ------------
    r_blob_tree& operator[](size_t index);
    const r_blob_tree& at(size_t index) const;
    bool has_index(size_t index) const;
    size_t size() const;

    // ------------ leaf setters ------------
    r_blob_tree& operator=(const std::string& value);
    r_blob_tree& operator=(const std::vector<uint8_t>& payload_storage);

    // ------------ leaf getters ------------
    const std::vector<uint8_t>& get_blob()   const;
    std::string                 get_string() const;
    template<typename T> T      get_value()  const;

    // ------------ misc ------------
    node_type type() const noexcept { return _type; }

private:
    // helpers for (de)serialisation (implemented in .cpp)
    static size_t _sizeof_treeb(const r_blob_tree& rt);
    static size_t _write_treeb(const r_blob_tree& rt, uint8_t* p, uint8_t* end);
    static size_t _read_treeb(const uint8_t* p, const uint8_t* end, r_blob_tree& rt);

    static inline size_t _bytes_left(const uint8_t* p, const uint8_t* end) { return static_cast<size_t>(end - p); }

    // internal helpers
    void _clear_value();
    void _ensure_type(node_type nt);
    void _expect_type(node_type nt) const;

    // data members
    node_type                        _type { NT_LEAF };
    std::map<std::string, r_blob_tree> _children;          // object
    std::vector<r_blob_tree>           _childrenByIndex;   // array
    std::vector<uint8_t>               _payload_storage;   // leaf
};

// ---- template impls ----
inline r_blob_tree& r_blob_tree::operator[](const std::string& key)
{
    _ensure_type(NT_OBJECT);
    return _children[key];
}

inline const r_blob_tree& r_blob_tree::at(const std::string& key) const
{
    _expect_type(NT_OBJECT);
    return _children.at(key);
}

inline bool r_blob_tree::has_key(const std::string& key) const
{
    _expect_type(NT_OBJECT);
    return _children.find(key) != _children.end();
}

inline r_blob_tree& r_blob_tree::operator[](size_t index)
{
    _ensure_type(NT_ARRAY);
    if(_childrenByIndex.size() < index + 1)
        _childrenByIndex.resize(index + 1);
    return _childrenByIndex[index];
}

inline const r_blob_tree& r_blob_tree::at(size_t index) const
{
    _expect_type(NT_ARRAY);
    return _childrenByIndex.at(index);
}

inline bool r_blob_tree::has_index(size_t index) const
{
    _expect_type(NT_ARRAY);
    return _childrenByIndex.size() > index;
}

inline size_t r_blob_tree::size() const
{
    switch(_type)
    {
        case NT_OBJECT: return _children.size();
        case NT_ARRAY : return _childrenByIndex.size();
        default       : return 0;
    }
}

inline r_blob_tree& r_blob_tree::operator=(const std::string& value)
{
    _clear_value();
    _type = NT_LEAF;
    _payload_storage.assign(value.begin(), value.end());
    return *this;
}

inline r_blob_tree& r_blob_tree::operator=(const std::vector<uint8_t>& payload_storage)
{
    _clear_value();
    _type = NT_LEAF;
    _payload_storage = payload_storage;
    return *this;
}

inline const std::vector<uint8_t>& r_blob_tree::get_blob() const
{
    _expect_type(NT_LEAF);
    return _payload_storage;
}

inline std::string r_blob_tree::get_string() const
{
    _expect_type(NT_LEAF);
    return std::string(reinterpret_cast<const char*>(_payload_storage.data()), _payload_storage.size());
}

template<typename T>
T r_blob_tree::get_value() const
{
    _expect_type(NT_LEAF);
    std::stringstream ss(get_string());
    T v{}; ss >> v;
    if(ss.fail())
        R_THROW(("r_blob_tree::get_value conversion failed"));
    return v;
}

inline void r_blob_tree::_clear_value()
{
    _children.clear();
    _childrenByIndex.clear();
    _payload_storage.clear();
}

inline void r_blob_tree::_ensure_type(node_type nt)
{
    if(_type == NT_LEAF)
        _type = nt;
    else if(_type != nt)
        R_STHROW(r_internal_exception,("r_blob_tree node type conflict"));
}

inline void r_blob_tree::_expect_type(node_type nt) const
{
    if(_type != nt)
        R_STHROW(r_internal_exception,("r_blob_tree node type mismatch"));
}

} // namespace r_utils

#endif // _r_utils_r_blob_tree_h
