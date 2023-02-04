#include "utils.h"
#include "r_utils/r_logger.h"
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <cstdlib>
#include <sys/types.h>
#ifdef IS_LINUX
#include <dirent.h>
#endif
#ifdef IS_WINDOWS
#include <Windows.h>
#include <tchar.h> 
#include <stdio.h>
#include <strsafe.h>
#endif

using namespace std;

void install_terminate()
{
    set_terminate(handle_terminate);
}

void handle_terminate()
{
    printf("terminate handler called!\n");
    fflush(stdout);
    R_LOG_ERROR("terminate handler called!\n");
    std::exception_ptr p = std::current_exception();
    if(p) {
        try {
            std::rethrow_exception(p);
        }
        catch(std::exception& ex) {
            printf("%s\n",ex.what());
            fflush(stdout);
            R_LOG_ERROR("terminate handler called! %s\n", ex.what());
        }
        catch(...) {
            printf("Unknown exception!");
            fflush(stdout);
            R_LOG_ERROR("terminate handler called!\n");
        }
    }
    fflush(stdout);
    std::abort();
}

vector<string> regular_files_in_dir(const string& dir)
{
    vector<string> names;

#ifdef IS_LINUX
    DIR* d = opendir(dir.c_str());
    if(!d)
        throw std::runtime_error("Unable to open directory");
    
    struct dirent* e = readdir(d);

    if(e)
    {
        do
        {
            string name(e->d_name);
            if(e->d_type == DT_REG && name != "." && name != "..")
                names.push_back(name);
            e = readdir(d);
        } while(e);
    }

    closedir(d);
#endif

#ifdef IS_WINDOWS
   WIN32_FIND_DATA ffd;
   TCHAR szDir[1024];
   HANDLE hFind;
   
   StringCchCopyA(szDir, 1024, dir.c_str());
   StringCchCatA(szDir, 1024, "\\*");

   // Find the first file in the directory.

   hFind = FindFirstFileA(szDir, &ffd);

   if (INVALID_HANDLE_VALUE == hFind) 
       throw std::runtime_error("Unable to open directory");

   do
   {
      if(!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
         names.push_back(string(ffd.cFileName)); 
   }
   while (FindNextFileA(hFind, &ffd) != 0);

   FindClose(hFind);
#endif

    return names;
}

bool ends_with(const string& a, const string& b)
{
    if (b.size() > a.size()) return false;
    return std::equal(a.begin() + a.size() - b.size(), a.end(), b.begin());
}
