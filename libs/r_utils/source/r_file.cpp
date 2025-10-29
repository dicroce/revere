
#include "r_utils/r_file.h"
#include "r_utils/r_string_utils.h"
#include <random>
#include <regex>
#include <algorithm>
#ifdef IS_WINDOWS
#include <Windows.h>
#include <Io.h>
#include <direct.h>
#else
#ifdef IS_LINUX
#include <fcntl.h>
#include <sys/statvfs.h>
#include <fnmatch.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#endif
#endif

using namespace r_utils;
using namespace std;

#ifdef IS_WINDOWS
//R_API const string r_utils::r_fs::PATH_SLASH = "\\";
#endif
#ifdef IS_LINUX
//R_API const string r_utils::r_fs::PATH_SLASH = "/";
#endif

r_file::r_file(r_file&& obj) noexcept :
    _f(std::move(obj._f))
{
    obj._f = nullptr;
}

r_file::~r_file() noexcept
{
    if(_f)
        fclose(_f);
}

r_file& r_file::operator = (r_file&& obj) noexcept
{
    if(_f)
        fclose(_f);

    _f = std::move(obj._f);
    obj._f = nullptr;
    return *this;
}

string r_utils::r_fs::current_exe_path()
{
    string output;
#ifdef IS_WINDOWS
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    output = r_string_utils::convert_wide_string_to_multi_byte_string(path);
#endif
#ifdef IS_LINUX
    char result[2048];
    ssize_t count = readlink("/proc/self/exe", result, 2048);
    output = string(result, (count > 0) ? count : 0);
#endif
    return output;
}

string r_utils::r_fs::platform_path(const string& path)
{
#ifdef IS_WINDOWS
    // convert forward slashes to backslashes
    auto s1 = regex_replace(path, regex("//"), "/");

    replace(begin(s1), end(s1), '/', '\\');
#endif
#ifdef IS_LINUX
    // convert backslashes to forward slashes
    auto s1 = regex_replace(path, regex("\\\\"), "\\");

    replace(begin(s1), end(s1), '\\', '/');
#endif
    return s1;
}

string r_utils::r_fs::working_directory()
{
    string output;
    char buf[4096];
#ifdef IS_WINDOWS
    if(_getcwd(buf, sizeof(buf)) != nullptr)
        output = buf;
#endif
#ifdef IS_LINUX
    if(getcwd(buf, sizeof(buf)) != nullptr)
        output = buf;
#endif
    return output;
}

void r_utils::r_fs::change_working_directory(const string& dir)
{
#ifdef IS_WINDOWS
    if(SetCurrentDirectory(dir.c_str()) == 0)
        R_STHROW(r_not_found_exception, ("Unable to change working directory to: %s", dir.c_str()));
#endif
#ifdef IS_LINUX
    if(chdir(dir.c_str()) < 0)
        R_STHROW(r_not_found_exception, ("Unable to change working directory to: %s", dir.c_str()));
#endif
}


string r_utils::r_fs::path_join(const string& path, const string& filename)
{
#ifdef IS_WINDOWS
    return path + "\\" + filename;
#endif
#ifdef IS_LINUX
    return path + "/" + filename;
#endif
}

int r_utils::r_fs::stat(const string& file_name, struct r_file_info* file_info)
{
#ifdef IS_WINDOWS
    struct __stat64 sfi;
    if( _wstat64( r_string_utils::convert_multi_byte_string_to_wide_string(file_name).data(), &sfi ) == 0 )
    {
        file_info->file_name = file_name;
        file_info->file_size = sfi.st_size;
        file_info->file_type = (sfi.st_mode & _S_IFDIR) ? R_DIRECTORY : R_REGULAR;
        return 0;
    }
    return -1;

#endif
#ifdef IS_LINUX
    struct stat sfi;
    if(::stat(file_name.c_str(), &sfi) == 0)
    {
        file_info->file_name = file_name;
        file_info->file_size = sfi.st_size;
        file_info->file_type = (sfi.st_mode & S_IFDIR) ? R_DIRECTORY : R_REGULAR;
        return 0;
    }

    return -1;
#endif
}

uint64_t r_utils::r_fs::file_size(const std::string& fileName)
{
    struct r_file_info file_info;
    int err = r_utils::r_fs::stat(fileName, &file_info);
    if(err < 0)
        R_STHROW(r_not_found_exception, ("Unable to stat: %s",fileName.c_str()));
    return file_info.file_size;
}

int r_utils::r_fs::fileno(FILE* f)
{
#ifdef IS_WINDOWS
    return _fileno(f);
#endif
#ifdef IS_LINUX
    return ::fileno(f);
#endif
}

vector<uint8_t> r_utils::r_fs::read_file(const string& path)
{
    struct r_file_info fi;
    if(r_utils::r_fs::stat(path, &fi) < 0)
        R_STHROW(r_not_found_exception, ("Unable to stat: %s", path.c_str()));

    uint32_t numBlocks = (fi.file_size > 4096) ? (uint32_t)(fi.file_size / 4096) : 0;
    uint32_t remainder = (fi.file_size > 4096) ? (uint32_t)(fi.file_size % 4096) : (uint32_t)fi.file_size;

    vector<uint8_t> out(fi.file_size);
    uint8_t* writer = &out[0];

    auto f = r_file::open(path, "rb");

    while(numBlocks > 0)
    {
        auto blocksRead = fread(writer, 4096, numBlocks, f);
        writer += blocksRead * 4096;
        numBlocks -= (uint32_t)blocksRead;
    }

    if(remainder > 0)
    {
        auto remainder_items_read = fread(writer, 1, remainder, f);
        if(remainder_items_read != remainder)
            R_STHROW(r_io_exception, ("Unable to read file: %s", path.c_str()));
    }

    return out;
}

void r_utils::r_fs::write_file(const uint8_t* bytes, size_t len, const string& path)
{
    auto f = r_file::open(path, "w+b");
    struct r_file_info fi;
    r_utils::r_fs::stat(path, &fi);

    uint32_t numBlocks = (len > 4096) ? (uint32_t)(len / 4096) : (uint32_t)0;
    uint32_t remainder = (len > 4096) ? (uint32_t)(len % 4096) : (uint32_t)len;

    while(numBlocks > 0)
    {
        auto blocksWritten = fwrite(bytes, 4096, numBlocks, f);
        bytes += blocksWritten * 4096;
        numBlocks -= (uint32_t)blocksWritten;
    }

    if(remainder > 0)
    {
        auto remainder_items_written = fwrite(bytes, 1, remainder, f);
        if(remainder_items_written != remainder)
            R_STHROW(r_io_exception, ("Unable to write file: %s", path.c_str()));
    }
}

void r_utils::r_fs::atomic_rename_file(const string& oldPath, const string& newPath)
{
    if(rename(oldPath.c_str(), newPath.c_str()) < 0)
        R_STHROW(r_internal_exception, ("Unable to rename %s to %s", oldPath.c_str(), newPath.c_str()));
}

bool r_utils::r_fs::file_exists(const string& path)
{
    struct r_utils::r_fs::r_file_info rfi;
    return (r_utils::r_fs::stat(path, &rfi) == 0) ? true : false;
}

bool r_utils::r_fs::is_reg(const string& path)
{
    struct r_utils::r_fs::r_file_info rfi;
    if(r_utils::r_fs::stat(path, &rfi) == 0)
    {
        if(rfi.file_type == r_utils::r_fs::R_REGULAR)
            return true;
    }

    return false;
}

bool r_utils::r_fs::is_dir(const string& path)
{
    struct r_utils::r_fs::r_file_info rfi;
    if(r_utils::r_fs::stat(path, &rfi) == 0)
    {
        if(rfi.file_type == r_utils::r_fs::R_DIRECTORY)
            return true;
    }

    return false;
}

int r_utils::r_fs::fallocate(FILE* file, uint64_t size)
{
#ifdef IS_WINDOWS
    LARGE_INTEGER li;
    li.QuadPart = size;
    auto moved = SetFilePointerEx(
        (HANDLE)_get_osfhandle(r_fs::fileno(file)),
        li,
        nullptr,
        FILE_BEGIN
    );
    if(moved == INVALID_SET_FILE_POINTER)
        return -1;
    SetEndOfFile((HANDLE)_get_osfhandle(r_fs::fileno(file)));
    return 0;
    //return ( _chsize_s( r_fs::fileno( file ), size ) == 0) ? 0 : -1;
#endif
#ifdef IS_LINUX
    return posix_fallocate64(r_fs::fileno(file), 0, size);
#endif
}

void r_utils::r_fs::break_path(const string& path, string& dir, string& fileName)
{
    auto p = path;
    while(r_utils::r_string_utils::ends_with(p, PATH_SLASH))
        p = p.substr(0, p.length() - 1);

    size_t rslash = p.rfind(PATH_SLASH);
    if(rslash == string::npos)
    {
        dir = "";
        fileName = p;
    }
    else
    {
        dir = p.substr(0, p.rfind(PATH_SLASH));
        fileName = p.substr(p.rfind(PATH_SLASH) + 1);
    }
}

string r_utils::r_fs::temp_file_name(const string& dir, const string& baseName)
{
    random_device rd;
    mt19937 mersenne_twister_rng(rd());
    uniform_int_distribution<int> uid(65, 90);

    string foundPath;

    while(true)
    {
        foundPath = (!dir.empty())? dir + PATH_SLASH : string(".") + PATH_SLASH;

        if(!baseName.empty())
            foundPath += baseName;

        for(int i = 0; i < 8; ++i)
            foundPath += (char)uid(mersenne_twister_rng);

        if(!r_utils::r_fs::file_exists(foundPath))
            return foundPath;
    }
}

void r_utils::r_fs::get_fs_usage(const string& path, uint64_t& size, uint64_t& free)
{
#ifdef IS_WINDOWS
    ULARGE_INTEGER winFree, winTotal;
    if (!GetDiskFreeSpaceExW(r_string_utils::convert_multi_byte_string_to_wide_string(path).data(), &winFree, &winTotal, 0))
        R_THROW(("Unable to get disk usage info."));
    size = winTotal.QuadPart;
    free = winFree.QuadPart;
#else
    struct statvfs stat;
    if (statvfs(path.c_str(), &stat) < 0)
        R_STHROW(r_not_found_exception, ("Unable to statvfs() path."));

    size = (uint64_t)stat.f_blocks * (uint64_t)stat.f_frsize;
    free = (uint64_t)stat.f_bavail * (uint64_t)stat.f_bsize;
#endif
}

void r_utils::r_fs::mkdir(const std::string& path)
{
#ifdef IS_WINDOWS
    if(_mkdir(path.c_str()) < 0)
        R_STHROW(r_internal_exception, ("Unable to make directory: %s",path.c_str()));
#else
    if(::mkdir(path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0)
        R_STHROW(r_internal_exception, ("Unable to make directory: %s",path.c_str()));
#endif
}

void r_utils::r_fs::rmdir(const std::string& path)
{
#ifdef IS_WINDOWS
    if(RemoveDirectoryA(path.c_str()) == 0)
        R_STHROW(r_internal_exception, ("Unable to remove directory: %s",path.c_str()));
#else
    if(::rmdir(path.c_str()) < 0)
        R_STHROW(r_internal_exception, ("Unable to remove directory: %s",path.c_str()));
#endif
}

void r_utils::r_fs::remove_file(const std::string& path)
{
#ifdef IS_WINDOWS
    if(DeleteFileA(path.c_str()) == 0)
        R_STHROW(r_internal_exception, ("Unable to remove file: %s",path.c_str()));
#else
    if(unlink(path.c_str()) != 0)
        R_STHROW(r_internal_exception, ("Unable to remove file: %s",path.c_str()));
#endif
}
