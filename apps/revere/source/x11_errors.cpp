#include "x11_errors.h"

#if defined(IS_LINUX) && defined(REVERE_HAVE_X11)

#include "r_utils/r_logger.h"
#include <SDL.h>
#include <SDL_syswm.h>
#include <X11/Xlib.h>
#include <cstdlib>

namespace
{

int g_randr_major_opcode = 0;
int g_randr_error_base = 0;
XErrorHandler g_previous_handler = nullptr;

int on_x_error(Display* display, XErrorEvent* e)
{
    if(e && revere::x_error_is_randr(e->error_code, g_randr_major_opcode, g_randr_error_base))
    {
        char text[128] = "";
        XGetErrorText(display, e->error_code, text, sizeof(text));
        R_LOG_WARNING("X11: ignoring a RandR error (%s; request %d.%d, serial %lu): a monitor was probably removed",
                      text, (int)e->request_code, (int)e->minor_code, e->serial);
        return 0;
    }

    if(g_previous_handler)
        return g_previous_handler(display, e);

    // No earlier handler to pass it to. Any other X error ends the app, as it would have without this handler:
    // hand it to Xlib's own (setting the handler to null puts Xlib's back, and the second call returns it), which
    // prints the error and exits.
    XSetErrorHandler(nullptr);
    XErrorHandler xlib_default = XSetErrorHandler(nullptr);
    if(xlib_default && xlib_default != on_x_error)
        xlib_default(display, e);
    R_LOG_ERROR("X11: an X error (code %d, request %d.%d) with no handler to pass it to; exiting",
                e ? (int)e->error_code : 0, e ? (int)e->request_code : 0, e ? (int)e->minor_code : 0);
    std::exit(1);
}

}

void revere::install_x11_error_handler(SDL_Window* window)
{
    if(!window)
        return;

    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if(!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_X11 || !info.info.x11.display)
        return;

    int event_base = 0;
    if(!XQueryExtension(info.info.x11.display, "RANDR", &g_randr_major_opcode, &event_base, &g_randr_error_base))
    {
        g_randr_major_opcode = 0;
        return;
    }

    auto previous = XSetErrorHandler(on_x_error);
    // Installed twice: keep the handler that was there before the first time.
    if(previous != on_x_error)
        g_previous_handler = previous;
}

#else

void revere::install_x11_error_handler(SDL_Window*)
{
}

#endif
