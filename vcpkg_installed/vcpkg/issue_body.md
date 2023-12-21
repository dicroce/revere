Package: x264[asm,core,default-features]:x86-windows -> 0.164.3095#2

**Host Environment**

- Host: x64-windows
- Compiler: MSVC 19.29.30151.0
-    vcpkg-tool version: 2022-12-14-7ae0d8527fb488fde10a89c2813802dc9b03b6f9
    vcpkg-scripts version: 877e3dc23 2023-01-16 (11 months ago)

**To Reproduce**

`vcpkg install `

**Failure logs**

```
-- Using cached mirror-x264-baee400fa9ced6f5481a728138fed6e867b0ff7f.tar.gz.
-- Cleaning sources at C:/dev/revere/vcpkg/buildtrees/x264/src/e867b0ff7f-fb2486f1d0.clean. Use --editable to skip cleaning for the packages you specify.
-- Extracting source C:/dev/revere/vcpkg/downloads/mirror-x264-baee400fa9ced6f5481a728138fed6e867b0ff7f.tar.gz
-- Applying patch uwp-cflags.patch
-- Applying patch parallel-install.patch
-- Applying patch allow-clang-cl.patch
-- Applying patch configure-as.patch
-- Using source at C:/dev/revere/vcpkg/buildtrees/x264/src/e867b0ff7f-fb2486f1d0.clean
-- Found external ninja('1.10.2').
-- Getting CMake variables for x86-windows
-- Getting CMake variables for x86-windows
-- Using cached msys-gzip-1.11-1-x86_64.pkg.tar.zst.
-- Downloading https://repo.msys2.org/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst;https://www2.futureware.at/~nickoe/msys2-mirror/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst;https://mirror.yandex.ru/mirrors/msys2/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst;https://mirrors.tuna.tsinghua.edu.cn/msys2/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst;https://mirrors.ustc.edu.cn/msys2/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst;https://mirror.bit.edu.cn/msys2/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst;https://mirror.selfnet.de/msys2/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst;https://mirrors.sjtug.sjtu.edu.cn/msys2/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst -> msys-bash-5.1.008-1-x86_64.pkg.tar.zst...
[DEBUG] To include the environment variables in debug output, pass --debug-env
[DEBUG] Feature flag 'binarycaching' unset
[DEBUG] Feature flag 'compilertracking' unset
[DEBUG] Feature flag 'registries' unset
[DEBUG] Feature flag 'versions' unset
Downloading https://repo.msys2.org/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst
Downloading https://www2.futureware.at/~nickoe/msys2-mirror/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst
Downloading https://mirror.yandex.ru/mirrors/msys2/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst
Downloading https://mirrors.tuna.tsinghua.edu.cn/msys2/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst
Downloading https://mirrors.ustc.edu.cn/msys2/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst
Downloading https://mirror.bit.edu.cn/msys2/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst
warning: Download failed -- retrying after 1000ms
warning: Download failed -- retrying after 2000ms
warning: Download failed -- retrying after 4000ms
Downloading https://mirror.selfnet.de/msys2/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst
Downloading https://mirrors.sjtug.sjtu.edu.cn/msys2/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst
error: Failed to download from mirror set
error: https://repo.msys2.org/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst: failed: status code 404
error: https://www2.futureware.at/~nickoe/msys2-mirror/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst: failed: status code 404
error: https://mirror.yandex.ru/mirrors/msys2/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst: failed: status code 404
error: https://mirrors.tuna.tsinghua.edu.cn/msys2/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst: failed: status code 404
error: https://mirrors.ustc.edu.cn/msys2/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst: failed: status code 404
error: https://mirror.bit.edu.cn/msys2/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst: WinHttpSendRequest failed with exit code 12007
error: https://mirror.bit.edu.cn/msys2/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst: WinHttpSendRequest failed with exit code 12007
error: https://mirror.bit.edu.cn/msys2/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst: WinHttpSendRequest failed with exit code 12007
error: https://mirror.bit.edu.cn/msys2/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst: WinHttpSendRequest failed with exit code 12007
error: https://mirror.selfnet.de/msys2/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst: failed: status code 404
error: https://mirrors.sjtug.sjtu.edu.cn/msys2/msys/x86_64/bash-5.1.008-1-x86_64.pkg.tar.zst: failed: status code 404
[DEBUG] D:\a\_work\1\s\src\vcpkg\base\downloads.cpp(956): 
[DEBUG] Time in subprocesses: 0 us
[DEBUG] Time in parsing JSON: 3 us
[DEBUG] Time in JSON reader: 0 us
[DEBUG] Time in filesystem: 13156 us
[DEBUG] Time in loading ports: 0 us
[DEBUG] Exiting after 17.87 s (17847702 us)

CMake Error at scripts/cmake/vcpkg_download_distfile.cmake:32 (message):
      
      Failed to download file with error: 1
      If you use a proxy, please check your proxy setting. Possible causes are:
      
      1. You are actually using an HTTP proxy, but setting HTTPS_PROXY variable
         to `https://address:port`. This is not correct, because `https://` prefix
         claims the proxy is an HTTPS proxy, while your proxy (v2ray, shadowsocksr
         , etc..) is an HTTP proxy. Try setting `http://address:port` to both
         HTTP_PROXY and HTTPS_PROXY instead.
      
      2. You are using Fiddler. Currently a bug (https://github.com/microsoft/vcpkg/issues/17752)
         will set HTTPS_PROXY to `https://fiddler_address:port` which lead to problem 1 above.
         Workaround is open Windows 10 Settings App, and search for Proxy Configuration page,
         Change `http=address:port;https=address:port` to `address`, and fill the port number.
      
      3. Your proxy's remote server is out of service.
      
      In future vcpkg releases, if you are using Windows, you no longer need to set
      HTTP(S)_PROXY environment variables. Vcpkg will simply apply Windows IE Proxy
      Settings set by your proxy software. See (https://github.com/microsoft/vcpkg-tool/pull/49)
      and (https://github.com/microsoft/vcpkg-tool/pull/77)
      
      Otherwise, please submit an issue at https://github.com/Microsoft/vcpkg/issues

Call Stack (most recent call first):
  scripts/cmake/vcpkg_download_distfile.cmake:273 (z_vcpkg_download_distfile_show_proxy_and_fail)
  scripts/cmake/vcpkg_acquire_msys.cmake:26 (vcpkg_download_distfile)
  scripts/cmake/vcpkg_acquire_msys.cmake:67 (z_vcpkg_acquire_msys_download_package)
  scripts/cmake/vcpkg_acquire_msys.cmake:161 (z_vcpkg_acquire_msys_declare_package)
  scripts/cmake/vcpkg_configure_make.cmake:207 (vcpkg_acquire_msys)
  ports/x264/portfile.cmake:88 (vcpkg_configure_make)
  scripts/ports.cmake:147 (include)



```


**Additional context**

<details><summary>vcpkg.json</summary>

```
{
  "name": "revere",
  "version-string": "1.0",
  "dependencies": [
    {
      "name": "ffmpeg",
      "features": [
        "avcodec",
        "avdevice",
        "avfilter",
        "avformat",
        "swresample",
        "swscale",
        "x264",
        "x265"
      ]
    },
    "gst-rtsp-server",
    {
      "name": "gstreamer",
      "features": [
        "plugins-bad",
        "plugins-base",
        "plugins-good",
        "plugins-ugly"
      ]
    },
    "libxml2",
    "glfw3"
  ]
}

```
</details>
