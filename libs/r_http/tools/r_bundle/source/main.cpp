
#include "r_utils/r_blob_tree.h"
#include "r_utils/r_string_utils.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <cstdio>

namespace fs = std::filesystem;

static const std::unordered_map<std::string, std::string> MIME_TYPES = {
    {".html",  "text/html; charset=utf-8"},
    {".htm",   "text/html; charset=utf-8"},
    {".css",   "text/css; charset=utf-8"},
    {".js",    "application/javascript; charset=utf-8"},
    {".mjs",   "application/javascript; charset=utf-8"},
    {".json",  "application/json"},
    {".map",   "application/json"},
    {".png",   "image/png"},
    {".jpg",   "image/jpeg"},
    {".jpeg",  "image/jpeg"},
    {".gif",   "image/gif"},
    {".svg",   "image/svg+xml"},
    {".ico",   "image/x-icon"},
    {".webp",  "image/webp"},
    {".woff",  "font/woff"},
    {".woff2", "font/woff2"},
    {".ttf",   "font/ttf"},
    {".eot",   "application/vnd.ms-fontobject"},
    {".txt",   "text/plain; charset=utf-8"},
    {".xml",   "application/xml"},
    {".pdf",   "application/pdf"},
};

static std::string mime_for(const fs::path& p)
{
    auto ext = r_utils::r_string_utils::to_lower(p.extension().string());
    auto it = MIME_TYPES.find(ext);
    return (it != MIME_TYPES.end()) ? it->second : "application/octet-stream";
}

// FNV-1a 64-bit — no external dependencies, good enough for cache-busting ETags
static uint64_t fnv1a_64(const std::vector<uint8_t>& data)
{
    uint64_t h = 14695981039346656037ULL;
    for(uint8_t b : data) { h ^= b; h *= 1099511628211ULL; }
    return h;
}

static std::string compute_etag(const std::vector<uint8_t>& data)
{
    char buf[17];
#ifdef _WIN32
    sprintf_s(buf, sizeof(buf), "%016llx", (unsigned long long)fnv1a_64(data));
#else
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)fnv1a_64(data));
#endif
    return std::string(buf);
}

static std::vector<uint8_t> read_file(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    if(!f)
        throw std::runtime_error("Cannot open file: " + p.string());
    return {std::istreambuf_iterator<char>(f), {}};
}

// Insert a file into the tree, creating nested object nodes for each path segment.
static void insert_file(r_utils::r_blob_tree& tree,
                        const fs::path& base,
                        const fs::path& file)
{
    auto rel = fs::relative(file, base);
    auto bytes = read_file(file);

    r_utils::r_blob_tree* node = &tree;
    for(auto it = rel.begin(); it != rel.end(); ++it)
        node = &((*node)[it->string()]);

    (*node)["bytes"] = bytes;
    (*node)["mime"]  = mime_for(file);
    (*node)["etag"]  = compute_etag(bytes);
}

int main(int argc, char* argv[])
{
    if(argc != 3)
    {
        std::cerr << "Usage: r_bundle <input_dir> <output.rbt>\n";
        return 1;
    }

    fs::path input_dir(argv[1]);
    fs::path output_file(argv[2]);

    if(!fs::is_directory(input_dir))
    {
        std::cerr << "Error: " << input_dir << " is not a directory\n";
        return 1;
    }

    r_utils::r_blob_tree tree;
    size_t file_count = 0;

    for(const auto& entry : fs::recursive_directory_iterator(input_dir))
    {
        if(!entry.is_regular_file())
            continue;

        try
        {
            insert_file(tree, input_dir, entry.path());
            std::cout << "  packed: " << fs::relative(entry.path(), input_dir).string() << "\n";
            ++file_count;
        }
        catch(const std::exception& ex)
        {
            std::cerr << "Warning: skipping " << entry.path() << ": " << ex.what() << "\n";
        }
    }

    uint32_t version = 1;
    auto serialized = r_utils::r_blob_tree::serialize(tree, version);

    std::ofstream out(output_file, std::ios::binary);
    if(!out)
    {
        std::cerr << "Error: cannot write " << output_file << "\n";
        return 1;
    }
    out.write(reinterpret_cast<const char*>(serialized.data()), serialized.size());

    std::cout << "Wrote " << file_count << " files ("
              << serialized.size() << " bytes) to "
              << output_file.string() << "\n";
    return 0;
}
