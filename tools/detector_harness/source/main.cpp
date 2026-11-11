// detector_harness: runs one object detection motion plugin (yolox, yolov8, ...)
// over a directory of images through its detect_objects C entry point, the
// way revere does (640x640 letterboxed RGB, see r_motion_engine.cpp), and
// writes every detection as CSV. Run it once per plugin and compare the CSVs
// (tools/detector_harness/compare.py) to calibrate one against the other.
//
//   detector_harness --plugin <dir>/motion_plugins/yolox_motion_plugin.dll
//                    --images <dir> [--out detections.csv] [--run-dir <dir>]
//
// --run-dir is where the plugin's dependencies (r_vss, ncnn, opencv...) and
// its models/ directory are; it defaults to the plugin directory's parent,
// i.e. a revere install or build tree. One plugin per process: two plugins
// from different trees would share whichever r_vss.dll loaded first.

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

typedef void* r_motion_plugin_handle;
typedef void* r_motion_event_plugin_host_handle;
typedef void (*r_object_detection_cb)(void* userdata, const char* class_name, float confidence,
                                      float x1, float y1, float x2, float y2);
typedef int (*abi_version_fn)();
typedef r_motion_plugin_handle (*load_plugin_fn)(r_motion_event_plugin_host_handle);
typedef void (*stop_plugin_fn)(r_motion_plugin_handle);
typedef void (*destroy_plugin_fn)(r_motion_plugin_handle);
typedef int (*detect_objects_fn)(r_motion_plugin_handle, const uint8_t*, size_t, uint16_t, uint16_t,
                                 r_object_detection_cb, void*);

namespace
{
const int FRAME_SIZE = 640;

struct found
{
    std::string class_name;
    float confidence;
    float x1, y1, x2, y2;
};

void on_detection(void* userdata, const char* class_name, float confidence,
                  float x1, float y1, float x2, float y2)
{
    static_cast<std::vector<found>*>(userdata)->push_back({class_name, confidence, x1, y1, x2, y2});
}

// As r_motion_engine.cpp: scale to fit, centre, black padding. RGB out.
std::vector<uint8_t> letterbox_rgb(const cv::Mat& bgr)
{
    const float scale = std::min((float)FRAME_SIZE / bgr.cols, (float)FRAME_SIZE / bgr.rows);
    const int sw = (int)(bgr.cols * scale), sh = (int)(bgr.rows * scale);
    cv::Mat scaled;
    cv::resize(bgr, scaled, cv::Size(sw, sh), 0, 0, cv::INTER_AREA);
    cv::Mat out = cv::Mat::zeros(FRAME_SIZE, FRAME_SIZE, CV_8UC3);
    scaled.copyTo(out(cv::Rect((FRAME_SIZE - sw) / 2, (FRAME_SIZE - sh) / 2, sw, sh)));
    cv::cvtColor(out, out, cv::COLOR_BGR2RGB);
    return std::vector<uint8_t>(out.data, out.data + out.total() * 3);
}

#ifdef _WIN32
void* open_library(const fs::path& p) { return (void*)LoadLibraryW(p.wstring().c_str()); }
void* symbol(void* lib, const char* name) { return (void*)GetProcAddress((HMODULE)lib, name); }
void close_library(void* lib) { FreeLibrary((HMODULE)lib); }
#else
void* open_library(const fs::path& p) { return dlopen(p.string().c_str(), RTLD_NOW | RTLD_LOCAL); }
void* symbol(void* lib, const char* name) { return dlsym(lib, name); }
void close_library(void* lib) { dlclose(lib); }
#endif

int usage()
{
    fprintf(stderr, "usage: detector_harness --plugin <plugin library> --images <dir> [--out <csv>] [--run-dir <dir>]\n");
    return 2;
}
}

int main(int argc, char** argv)
{
    fs::path plugin_path, images_dir, out_path, run_dir;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (i + 1 >= argc) return usage();
        if (a == "--plugin") plugin_path = argv[++i];
        else if (a == "--images") images_dir = argv[++i];
        else if (a == "--out") out_path = argv[++i];
        else if (a == "--run-dir") run_dir = argv[++i];
        else return usage();
    }
    if (plugin_path.empty() || images_dir.empty()) return usage();

    plugin_path = fs::absolute(plugin_path);
    images_dir = fs::absolute(images_dir);
    if (!out_path.empty()) out_path = fs::absolute(out_path);
    run_dir = run_dir.empty() ? plugin_path.parent_path().parent_path() : fs::absolute(run_dir);

    std::vector<fs::path> images;
    for (const auto& e : fs::directory_iterator(images_dir))
    {
        auto ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (e.is_regular_file() && (ext == ".jpg" || ext == ".jpeg" || ext == ".png"))
            images.push_back(e.path());
    }
    std::sort(images.begin(), images.end());
    if (images.empty())
    {
        fprintf(stderr, "no images in %s\n", images_dir.string().c_str());
        return 1;
    }

    // The plugin finds models/ from the working directory, and its
    // dependencies beside revere.
    fs::current_path(run_dir);
#ifdef _WIN32
    SetDllDirectoryW(run_dir.wstring().c_str());
#endif

    void* lib = open_library(plugin_path);
    if (!lib)
    {
        fprintf(stderr, "can't load %s\n", plugin_path.string().c_str());
        return 1;
    }
    auto abi_version = (abi_version_fn)symbol(lib, "motion_plugin_abi_version");
    auto load_plugin = (load_plugin_fn)symbol(lib, "load_plugin");
    auto stop_plugin = (stop_plugin_fn)symbol(lib, "stop_plugin");
    auto destroy_plugin = (destroy_plugin_fn)symbol(lib, "destroy_plugin");
    auto detect_objects = (detect_objects_fn)symbol(lib, "detect_objects");
    if (!load_plugin || !destroy_plugin || !detect_objects)
    {
        fprintf(stderr, "%s doesn't export load_plugin/destroy_plugin/detect_objects\n", plugin_path.string().c_str());
        return 1;
    }
    fprintf(stderr, "plugin %s (abi %d), run dir %s, %zu images\n", plugin_path.filename().string().c_str(),
            abi_version ? abi_version() : -1, run_dir.string().c_str(), images.size());

    // No host: detect_objects needs none (static anchors aren't loaded).
    r_motion_plugin_handle plugin = load_plugin(nullptr);
    if (!plugin)
    {
        fprintf(stderr, "load_plugin returned null\n");
        return 1;
    }

    FILE* out = out_path.empty() ? stdout : fopen(out_path.string().c_str(), "w");
    if (!out)
    {
        fprintf(stderr, "can't write %s\n", out_path.string().c_str());
        return 1;
    }
    fprintf(out, "image,class_name,confidence,x1,y1,x2,y2\n");

    double total_ms = 0;
    size_t n_run = 0, n_found = 0;
    for (const auto& p : images)
    {
        cv::Mat bgr = cv::imread(p.string());
        if (bgr.empty())
        {
            fprintf(stderr, "can't read %s\n", p.string().c_str());
            continue;
        }
        auto frame = letterbox_rgb(bgr);

        std::vector<found> dets;
        const auto t0 = std::chrono::steady_clock::now();
        const int rc = detect_objects(plugin, frame.data(), frame.size(), FRAME_SIZE, FRAME_SIZE, on_detection, &dets);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (rc != 0)
        {
            fprintf(stderr, "detect_objects failed (%d) on %s\n", rc, p.string().c_str());
            continue;
        }
        // The first call warms up (allocations, caches); keep it out of the average.
        if (n_run > 0) total_ms += ms;
        ++n_run;
        n_found += dets.size();

        // A row with no class marks an image with no detections.
        if (dets.empty())
            fprintf(out, "%s,,,,,,\n", p.filename().string().c_str());
        for (const auto& d : dets)
            fprintf(out, "%s,%s,%.4f,%.1f,%.1f,%.1f,%.1f\n", p.filename().string().c_str(), d.class_name.c_str(),
                    d.confidence, d.x1, d.y1, d.x2, d.y2);
    }
    if (out != stdout) fclose(out);

    fprintf(stderr, "%zu images, %zu detections, %.1f ms/image average inference\n", n_run, n_found,
            n_run > 1 ? total_ms / (n_run - 1) : 0.0);

    if (stop_plugin) stop_plugin(plugin);
    destroy_plugin(plugin);
    close_library(lib);
    return 0;
}
