#include "test_render_policy.h"
#include "render_policy.h"
#include "r_ui_utils/texture.h"
#include <SDL.h>
#include <vector>
#include <string>

using namespace revere;
using namespace std;

REGISTER_TEST_FIXTURE(test_render_policy);

void test_render_policy::test_renderer_mode_from_env()
{
    RTF_ASSERT(renderer_mode_from(nullptr) == renderer_mode::automatic);
    RTF_ASSERT(renderer_mode_from("") == renderer_mode::automatic);
    RTF_ASSERT(renderer_mode_from("auto") == renderer_mode::automatic);
    RTF_ASSERT(renderer_mode_from("anything else") == renderer_mode::automatic);
    RTF_ASSERT(renderer_mode_from("software") == renderer_mode::software);
    RTF_ASSERT(renderer_mode_from("SOFTWARE") == renderer_mode::software);
    RTF_ASSERT(renderer_mode_from("accelerated") == renderer_mode::accelerated);
    RTF_ASSERT(renderer_mode_from("gpu") == renderer_mode::accelerated);
}

void test_render_policy::test_attempts_per_platform()
{
    // macOS: Metal first, then SDL's own pick, then software last.
    auto mac = renderer_attempts(renderer_mode::automatic, render_platform::macos);
    RTF_ASSERT(mac.size() == 3);
    RTF_ASSERT(mac[0].accelerated && mac[0].driver == "metal");
    RTF_ASSERT(mac[1].accelerated && mac[1].driver.empty());
    RTF_ASSERT(!mac[2].accelerated && mac[2].driver == "software");

    // Windows and Linux stay software-first; an accelerated renderer only if software can't start.
    for(auto p : {render_platform::windows, render_platform::linux_})
    {
        auto a = renderer_attempts(renderer_mode::automatic, p);
        RTF_ASSERT(a.size() == 2);
        RTF_ASSERT(!a[0].accelerated && a[0].driver == "software");
        RTF_ASSERT(a[1].accelerated && a[1].driver.empty());
    }
}

void test_render_policy::test_forced_modes()
{
    for(auto p : {render_platform::macos, render_platform::windows, render_platform::linux_})
    {
        // Forced software: software alone.
        auto s = renderer_attempts(renderer_mode::software, p);
        RTF_ASSERT(s.size() == 1);
        RTF_ASSERT(!s[0].accelerated);
        // Opted in to accelerated: GPU attempts only, no software fallback.
        auto a = renderer_attempts(renderer_mode::accelerated, p);
        RTF_ASSERT(!a.empty());
        for(const auto& x : a)
            RTF_ASSERT(x.accelerated);
    }
    auto win = renderer_attempts(renderer_mode::accelerated, render_platform::windows);
    RTF_ASSERT(win[0].driver == "direct3d11" && win[1].driver == "direct3d");
    auto lin = renderer_attempts(renderer_mode::accelerated, render_platform::linux_);
    RTF_ASSERT(lin[0].driver == "opengl");
}

void test_render_policy::test_window_drawable()
{
    RTF_ASSERT(window_drawable(true, false, false));
    RTF_ASSERT(!window_drawable(false, false, false));  // hidden to the tray
    RTF_ASSERT(!window_drawable(true, true, false));    // minimised
    RTF_ASSERT(!window_drawable(true, false, true));    // covered, on another desktop, or hidden with the app
    RTF_ASSERT(!window_drawable(false, true, true));
}

void test_render_policy::test_hash_tells_frames_apart()
{
    const string a = "frame one";
    const string b = "frame two";
    RTF_ASSERT(hash_bytes(a.data(), a.size()) == hash_bytes(a.data(), a.size()));
    RTF_ASSERT(hash_bytes(a.data(), a.size()) != hash_bytes(b.data(), b.size()));
    // One changed byte, and the order of the parts, both change it.
    string c = a; c[3] = 'X';
    RTF_ASSERT(hash_bytes(a.data(), a.size()) != hash_bytes(c.data(), c.size()));
    const auto ab = hash_bytes(b.data(), b.size(), hash_bytes(a.data(), a.size()));
    const auto ba = hash_bytes(a.data(), a.size(), hash_bytes(b.data(), b.size()));
    RTF_ASSERT(ab != ba);
    // Values hash by their bytes.
    RTF_ASSERT(hash_value(1.5f, 7) == hash_value(1.5f, 7));
    RTF_ASSERT(hash_value(1.5f, 7) != hash_value(2.5f, 7));
    RTF_ASSERT(hash_bytes(nullptr, 0) == 1469598103934665603ULL);
}

void test_render_policy::test_frame_skipper()
{
    frame_skipper f;
    // The first frame is always drawn; the same one again is not; a different one is.
    RTF_ASSERT(f.should_render(11));
    RTF_ASSERT(!f.should_render(11));
    RTF_ASSERT(!f.should_render(11));
    RTF_ASSERT(f.should_render(12));
    RTF_ASSERT(!f.should_render(12));
    // Back to an earlier frame: that is a change from the one on the window.
    RTF_ASSERT(f.should_render(11));
    RTF_ASSERT(f.rendered() == 3);
    RTF_ASSERT(f.skipped() == 3);
}

void test_render_policy::test_frame_skipper_invalidate()
{
    frame_skipper f;
    RTF_ASSERT(f.should_render(5));
    RTF_ASSERT(!f.should_render(5));
    // The window was exposed, resized, shown again, or the GPU device reset: what it shows is not known any more.
    f.invalidate();
    RTF_ASSERT(f.should_render(5));
    RTF_ASSERT(!f.should_render(5));
    f.invalidate();
    f.invalidate();
    RTF_ASSERT(f.should_render(6));
}

void test_render_policy::test_texture_generation_moves()
{
    // A texture created, or updated in place with the same id, moves the counter the frame hash includes.
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 8, 8, 32, SDL_PIXELFORMAT_ARGB8888);
    RTF_ASSERT(surface != nullptr);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    RTF_ASSERT(renderer != nullptr);
    std::vector<uint8_t> pixels(4 * 4 * 4, 255);

    const auto g0 = r_ui_utils::texture_generation().load();
    auto tex = r_ui_utils::texture::create_from_rgba(renderer, pixels.data(), 4, 4);
    RTF_ASSERT(tex != nullptr);
    const auto g1 = r_ui_utils::texture_generation().load();
    RTF_ASSERT(g1 > g0);

    auto streaming = r_ui_utils::texture::create_streaming(renderer, 4, 4);
    RTF_ASSERT(streaming != nullptr);
    const auto g2 = r_ui_utils::texture_generation().load();
    RTF_ASSERT(streaming->update_rgba(pixels.data(), 4, 4));
    RTF_ASSERT(r_ui_utils::texture_generation().load() > g2);

    streaming.reset();
    tex.reset();
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
}
