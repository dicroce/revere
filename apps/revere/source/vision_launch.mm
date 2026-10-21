
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
        cmd += (cmd.empty() ? "" : " ") + a;
        argv.push_back(const_cast<char*>(a.c_str()));
    }
    argv.push_back(nullptr);

    int fds[2];
    if(pipe(fds) != 0)
    {
        R_LOG_ERROR("launch failed: pipe(): %s (%s)", strerror(errno), cmd.c_str());
        return false;
    }
    // Keep the pipe out of anything else revere starts meanwhile.
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fa, fds[1], STDERR_FILENO);
    pid_t pid = 0;
    int err = posix_spawn(&pid, argv[0], &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    close(fds[1]);
    if(err != 0)
    {
        close(fds[0]);
        R_LOG_ERROR("launch failed: posix_spawn: %s (%s)", strerror(err), cmd.c_str());
        return false;
    }
    R_LOG_INFO("launch: %s", cmd.c_str());

    thread([pid, fd = fds[0], cmd, on_exit](){
        string output;
        char buf[512];
        ssize_t n;
        while((n = read(fd, buf, sizeof(buf))) > 0 || (n < 0 && errno == EINTR))
            if(n > 0 && output.size() < 4096)
                output.append(buf, (size_t)n);
        close(fd);
        int status = 0;
        while(waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        if(code != 0)
        {
            while(!output.empty() && (output.back() == '\n' || output.back() == '\r'))
                output.pop_back();
            R_LOG_ERROR("launch failed: open exited with %d: %s (%s)", code, output.c_str(), cmd.c_str());
        }
        if(on_exit)
            on_exit(code);
    }).detach();
    return true;
}
