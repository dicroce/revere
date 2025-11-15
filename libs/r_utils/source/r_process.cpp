
#include "r_utils/r_process.h"
#include "r_utils/r_file.h"
#include <string.h>
#include <vector>

using namespace r_utils;
using namespace std;
using namespace std::chrono;

r_process::r_process(const string& cmd, bool detached) :
    _pid(),
    _cmd(cmd),
    _detached(detached)
{
}

r_process::~r_process() noexcept
{
    if(_pid.valid())
    {
#if defined(IS_LINUX) || defined(IS_MACOS)
        int status;
        waitpid(_pid.pid, &status, 0);
#endif
#ifdef IS_WINDOWS
        WaitForSingleObject(_pid.pi.hProcess, INFINITE);
        CloseHandle(_pid.pi.hProcess);
        CloseHandle(_pid.pi.hThread);
#endif
    }
}

void r_process::start()
{
#if defined(IS_LINUX) || defined(IS_MACOS)
    if (_detached)
    {
#ifdef IS_MACOS
        // On macOS, if using 'open' command, just use system() - it handles detachment perfectly
        if(_cmd.find("open ") == 0) {
            // Append & to run in background and redirect output to /dev/null to fully detach
            string cmd = _cmd + " > /dev/null 2>&1 &";
            system(cmd.c_str());
            _pid.clear(); // Can't track this process
            return;
        }
#endif
        // Use double-fork technique to avoid zombies
        _pid.pid = fork();
        if(_pid.pid < 0)
            R_THROW(("Unable to fork()."));

        if(_pid.pid == 0) // First child
        {
            if(fork() == 0) // Second child (grandchild)
            {
                setsid(); // Create new session
                // Execute the command in grandchild
                vector<string> parts;

                // work left to right
                // if you see whitespace, make command part
                // if you see quote, find terminating quote and make command part

                size_t ps=0, pe=0, cmdLen = _cmd.length();
                while(ps < cmdLen)
                {
                    bool inQuote = false;

                    while(_cmd[ps] == ' ' && ps < cmdLen)
                    {
                        ++ps;
                        pe=ps;
                    }

                    if(_cmd[ps] == '"')
                        inQuote = true;

                    while((inQuote || _cmd[pe] != ' ') && pe < cmdLen)
                    {
                        ++pe;
                        if(_cmd[pe] == '"')
                        {
                            if(inQuote)
                            {
                                // push command ps+1 -> pe
                                parts.push_back(string(&_cmd[ps+1],pe-(ps+1)));
                                inQuote = false;
                                ps = pe+1;
                                break;
                            }
                            else
                            {
                                inQuote = true;
                            }
                        }
                        else if((!inQuote && _cmd[pe] == ' ') || pe == cmdLen)
                        {
                            // push command ps -> pe
                            parts.push_back(string(&_cmd[ps],pe-ps));
                            ps = pe;
                            break;
                        }
                    }
                }

                vector<const char*> partPtrs;
                for( auto& p : parts )
                    partPtrs.push_back(p.c_str());
                partPtrs.push_back(NULL);
#ifdef IS_MACOS
                // On macOS, if the command starts with "open", use sh -c to execute it
                if(parts.size() > 0 && parts[0] == "open") {
                    execl("/bin/sh", "sh", "-c", _cmd.c_str(), NULL);
                } else {
                    execv(parts[0].c_str(), (char* const*)&partPtrs[0]);
                }
#else
                execv(parts[0].c_str(), (char* const*)&partPtrs[0]);
#endif
                R_THROW(("Failed to execve()."));
            }
            else
            {
                exit(0); // First child exits immediately
            }
        }
        else
        {
            // Parent waits for first child only
            int status;
            waitpid(_pid.pid, &status, 0);
            _pid.clear(); // Clear since we won't track the grandchild
        }
    }
    else
    {
        // Regular single fork for non-detached processes
        _pid.pid = fork();
        if(_pid.pid < 0)
            R_THROW(("Unable to fork()."));

        if(_pid.pid == 0) // 0 is returned in child...
        {
            vector<string> parts;

            // work left to right
            // if you see whitespace, make command part
            // if you see quote, find terminating quote and make command part

            size_t ps=0, pe=0, cmdLen = _cmd.length();
            while(ps < cmdLen)
            {
                bool inQuote = false;

                while(_cmd[ps] == ' ' && ps < cmdLen)
                {
                    ++ps;
                    pe=ps;
                }

                if(_cmd[ps] == '"')
                    inQuote = true;

                while((inQuote || _cmd[pe] != ' ') && pe < cmdLen)
                {
                    ++pe;
                    if(_cmd[pe] == '"')
                    {
                        if(inQuote)
                        {
                            // push command ps+1 -> pe
                            parts.push_back(string(&_cmd[ps+1],pe-(ps+1)));
                            inQuote = false;
                            ps = pe+1;
                            break;
                        }
                        else
                        {
                            inQuote = true;
                        }
                    }
                    else if((!inQuote && _cmd[pe] == ' ') || pe == cmdLen)
                    {
                        // push command ps -> pe
                        parts.push_back(string(&_cmd[ps],pe-ps));
                        ps = pe;
                        break;
                    }
                }
            }

            vector<const char*> partPtrs;
            for( auto& p : parts )
                partPtrs.push_back(p.c_str());
            partPtrs.push_back(NULL);
#ifdef IS_MACOS
            // On macOS, if the command starts with "open", use sh -c to execute it
            if(parts.size() > 0 && parts[0] == "open") {
                execl("/bin/sh", "sh", "-c", _cmd.c_str(), NULL);
            } else {
                execv(parts[0].c_str(), (char* const*)&partPtrs[0]);
            }
#else
            execv(parts[0].c_str(), (char* const*)&partPtrs[0]);
#endif
            R_THROW(("Failed to execve()."));
        }
    }
#endif
#ifdef IS_WINDOWS
    STARTUPINFO si;
    ZeroMemory( &si, sizeof(si) );
    si.cb = sizeof(si);
    ZeroMemory( &_pid.pi, sizeof(_pid.pi) );

    if(!CreateProcess(
        NULL,           // No module name (use command line)
        (LPSTR)_cmd.c_str(),     // Command line
        NULL,           // Process handle not inheritable
        NULL,           // Thread handle not inheritable
        FALSE,          // Set handle inheritance to FALSE
        0,              // No creation flags
        NULL,           // Use parent's environment block
        NULL,           // Use parent's starting directory 
        &si,            // Pointer to STARTUPINFO structure
        &_pid.pi        // Pointer to PROCESS_INFORMATION structure
    ))
    {
        R_THROW(( "CreateProcess failed (%d).\n", GetLastError()));
        return;
    }
#endif
}

bool r_process::running()
{
    if (_detached)
    {
        return false; // Cannot check status of detached processes
    }
    
    if(_pid.valid())
    {
        int code;
        wait_for(code, milliseconds(1));
        return _pid.valid();
    }
    return false;
}

r_wait_status r_process::wait_for(int& code, milliseconds timeout)
{
#if defined(IS_LINUX) || defined(IS_MACOS)
    auto remaining = timeout;
    while(duration_cast<milliseconds>(remaining).count() > 0)
    {
        auto before = steady_clock::now();

        int status;
        int res = waitpid(_pid.pid, &status, WNOHANG);
        if( res > 0 )
        {
            code = WEXITSTATUS(status);
            _pid.clear();
            return R_PROCESS_EXITED;
        }

        if( res < 0 )
            R_THROW(("Unable to waitpid()."));

        usleep(250000);

        auto after = steady_clock::now();

        auto delta = duration_cast<milliseconds>(after - before);

        if( remaining > delta )
            remaining -= delta;
        else remaining = milliseconds::zero();
    }
#endif
#ifdef IS_WINDOWS
    auto result = WaitForSingleObject(_pid.pi.hProcess, (DWORD)duration_cast<milliseconds>(timeout).count());
    if(result == WAIT_OBJECT_0)
    {
        CloseHandle(_pid.pi.hProcess);
        CloseHandle(_pid.pi.hThread);
        _pid.clear();
        return R_PROCESS_EXITED;
    }
#endif

    return R_PROCESS_WAIT_TIMEDOUT;
}

r_wait_status r_process::wait(int& code)
{
#if defined(IS_LINUX) || defined(IS_MACOS)
    int status;
    waitpid(_pid.pid, &status, 0);
#endif
#ifdef IS_WINDOWS
    WaitForSingleObject(_pid.pi.hProcess, INFINITE);
    CloseHandle(_pid.pi.hProcess);
    CloseHandle(_pid.pi.hThread);
#endif
    _pid.clear();
    return R_PROCESS_EXITED;
}

void r_process::kill()
{
#if defined(IS_LINUX) || defined(IS_MACOS)
    ::kill(-_pid.pid, SIGKILL); //negated pid means kill entire process group id
#endif
#ifdef IS_WINDOWS
    TerminateProcess(_pid.pi.hProcess, 0);
    CloseHandle(_pid.pi.hProcess);
    CloseHandle(_pid.pi.hThread);
#endif
    _pid.clear();
}
