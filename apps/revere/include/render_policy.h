#ifndef __revere_render_policy_h
#define __revere_render_policy_h

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

// How the window is drawn, and when it isn't. No SDL or ImGui here, so it is tested on its own (ut/test_render_policy).
//
// Drawing the whole window is the app's one real cost when it is idle: with SDL's software renderer every frame is
// rasterised on the CPU. So the renderer is a GPU one where there is one (software is the fallback), nothing is drawn
// while the window can't be seen, and a frame that came out the same as the last one is not drawn again.

namespace revere
{

enum class renderer_mode { automatic, software, accelerated };
enum class render_platform { macos, windows, linux_ };

// REVERE_RENDERER=software or accelerated forces one; anything else (or nothing) is automatic. An escape hatch for a
// machine whose GPU driver misbehaves.
inline renderer_mode renderer_mode_from(const char* value)
{
    if(!value)
        return renderer_mode::automatic;
    const std::string v(value);
    if(v == "software" || v == "SOFTWARE")
        return renderer_mode::software;
    if(v == "accelerated" || v == "ACCELERATED" || v == "gpu" || v == "GPU")
        return renderer_mode::accelerated;
    return renderer_mode::automatic;
}

struct renderer_attempt
{
    bool accelerated {false};
    // SDL_HINT_RENDER_DRIVER while this one is tried; empty: SDL chooses.
    std::string driver;
};

// What to try, in order. Automatic: the platform's GPU renderer (Metal on macOS, Direct3D on Windows, OpenGL on Linux),
// then SDL's own accelerated pick, then software. Forced software is software alone; forced accelerated is the GPU
// attempts without the software fallback.
inline std::vector<renderer_attempt> renderer_attempts(renderer_mode mode, render_platform platform)
{
    if(mode == renderer_mode::software)
        return { {false, "software"} };

    std::vector<renderer_attempt> out;
    switch(platform)
    {
        case render_platform::macos: out.push_back({true, "metal"}); break;
        case render_platform::windows: out.push_back({true, "direct3d11"}); out.push_back({true, "direct3d"}); break;
        case render_platform::linux_: out.push_back({true, "opengl"}); break;
    }
    out.push_back({true, ""});
    if(mode == renderer_mode::automatic)
        out.push_back({false, "software"});
    return out;
}

// The window can be seen: shown (not hidden to the tray, not minimised) and not covered, on another desktop or hidden
// with the app (occluded). Nothing is drawn, and none of the UI's work is done, while it can't.
inline bool window_drawable(bool shown, bool minimised, bool occluded)
{
    return shown && !minimised && !occluded;
}

// FNV-1a, 64 bits. Not for security: it only has to tell two frames apart.
inline uint64_t hash_bytes(const void* data, size_t size, uint64_t seed = 1469598103934665603ULL)
{
    const auto* p = static_cast<const unsigned char*>(data);
    uint64_t h = seed;
    for(size_t i = 0; i < size; ++i)
    {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

template<typename T>
inline uint64_t hash_value(const T& v, uint64_t seed)
{
    return hash_bytes(&v, sizeof(T), seed);
}

// Decides whether a built frame has to be drawn: not when it is the same as the one on the window, unless something
// has made the window's contents unknown (it was exposed, resized, shown again, the GPU device was reset).
class frame_skipper final
{
public:
    // `hash` of the frame about to be drawn. True: draw and present it. False: the window already shows it.
    bool should_render(uint64_t hash)
    {
        if(_valid && hash == _last)
        {
            ++_skipped;
            return false;
        }
        _valid = true;
        _last = hash;
        ++_rendered;
        return true;
    }

    // What the window shows is no longer known: the next frame is drawn whatever it is.
    void invalidate() { _valid = false; }

    uint64_t rendered() const { return _rendered; }
    uint64_t skipped() const { return _skipped; }

private:
    bool _valid {false};
    uint64_t _last {0};
    uint64_t _rendered {0};
    uint64_t _skipped {0};
};

}

#endif
