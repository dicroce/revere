
#include "vision_launch.h"
#include "r_utils/r_logger.h"
#import <Foundation/Foundation.h>
#include <spawn.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <thread>
#include <atomic>
#include <fstream>
#include <climits>
#include <cctype>

extern char** environ;

using namespace std;

string app_bundle_executable(const string& app_path)
{
    @autoreleasepool
    {
        NSString* app = [NSString stringWithUTF8String:app_path.c_str()];
        NSDictionary* info = [NSDictionary dictionaryWithContentsOfFile:[app stringByAppendingPathComponent:@"Contents/Info.plist"]];
        NSString* name = info[@"CFBundleExecutable"];
        if(![name isKindOfClass:[NSString class]] || name.length == 0)
            return string();
        NSString* exe = [[app stringByAppendingPathComponent:@"Contents/MacOS"] stringByAppendingPathComponent:name];
        if(![[NSFileManager defaultManager] isExecutableFileAtPath:exe])
            return string();
        return string(exe.fileSystemRepresentation);
    }
}

string find_vision_app(const string& exe_dir)
{
    const string candidates[] = {exe_dir + "/../../../vision.app", "/Applications/Vision.app"};
    for(const auto& app : candidates)
        if(!app_bundle_executable(app).empty())
            return app;
    return string();
}

// For logging: the argument as a shell would need it, single-quoted if it has
// anything outside a plain-word set.
static string _shell_quoted(const string& a)
{
    bool plain = !a.empty();
    for(char c : a)
        if(!isalnum((unsigned char)c) && !strchr("_-./:=@%+,", c))
            plain = false;
    if(plain)
        return a;
    string q = "'";
    for(char c : a)
        q += (c == '\'') ? string("'\\''") : string(1, c);
    return q + "'";
}

// Where open's output goes: a file the child creates itself, so revere never
// holds an fd for it that something else it starts could inherit.
static string _output_path()
{
    static atomic<unsigned> n {0};
    char dir[PATH_MAX];
    string d = confstr(_CS_DARWIN_USER_TEMP_DIR, dir, sizeof(dir)) > 0 ? dir : "/tmp/";
    if(d.back() != '/')
        d += '/';
    return d + "revere-open-" + to_string(getpid()) + "-" + to_string(++n) + ".log";
}

bool open_app(const string& app, const vector<string>& args, void (*on_exit)(int code))
{
    vector<string> argv_s = {"/usr/bin/open", "-n", app};
    if(!args.empty())
    {
        argv_s.push_back("--args");
        argv_s.insert(argv_s.end(), args.begin(), args.end());
    }
    string cmd;
    vector<char*> argv;
    for(auto& a : argv_s)
    {
        cmd += (cmd.empty() ? "" : " ") + _shell_quoted(a);
        argv.push_back(const_cast<char*>(a.c_str()));
    }
    argv.push_back(nullptr);

    auto out_path = _output_path();
    unlink(out_path.c_str());

    // open inherits nothing but stdin (/dev/null) and its output file.
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_CLOEXEC_DEFAULT);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, out_path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    posix_spawn_file_actions_adddup2(&fa, STDOUT_FILENO, STDERR_FILENO);
    pid_t pid = 0;
    int err = posix_spawn(&pid, argv[0], &fa, &attr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);
    if(err != 0)
    {
        unlink(out_path.c_str());
        R_LOG_ERROR("launch failed: posix_spawn: %s (%s)", strerror(err), cmd.c_str());
        return false;
    }
    R_LOG_INFO("launch: %s", cmd.c_str());

    thread([pid, out_path, cmd, on_exit](){
        int status = 0;
        while(waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        if(code != 0)
        {
            string output;
            ifstream f(out_path);
            char buf[4096];
            f.read(buf, sizeof(buf));
            output.assign(buf, (size_t)f.gcount());
            while(!output.empty() && (output.back() == '\n' || output.back() == '\r'))
                output.pop_back();
            R_LOG_ERROR("launch failed: open exited with %d: %s (%s)", code, output.c_str(), cmd.c_str());
        }
        unlink(out_path.c_str());
        if(on_exit)
            on_exit(code);
    }).detach();
    return true;
}
