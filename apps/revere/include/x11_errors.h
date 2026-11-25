#ifndef __revere_x11_errors_h
#define __revere_x11_errors_h

struct SDL_Window;

// X errors the desktop app can live through. No Xlib here, so the decision is tested on its own
// (ut/test_x11_errors).
//
// Xlib's default error handler prints the error and calls exit(): one failed request ends the app, with no crash
// report and nothing in the log. RandR requests fail in ordinary use. When a monitor is unplugged, switched off or
// goes to sleep, SDL asks about an output the X server has just removed (RRGetOutputInfo) and gets BadRROutput.
// SDL already copes with that request returning nothing; only the error handler made it fatal.

namespace revere
{

// The errors RandR defines, counted from its error base: BadRROutput, BadRRCrtc, BadRRMode, BadRRProvider.
constexpr int RANDR_ERROR_COUNT = 4;

// Whether an X error is one of RandR's own (BadRROutput and the three beside it), on whatever request it came back.
// A core error on a RandR request (BadWindow from RRSelectInput, say) is not: that is a bug, and stays fatal.
// randr_major_opcode and randr_error_base are what XQueryExtension gave for "RANDR"; a major opcode of 0 means the
// server has no RandR, and nothing is RandR's.
inline bool x_error_is_randr(int error_code, int randr_major_opcode, int randr_error_base)
{
    if(randr_major_opcode <= 0)
        return false;
    return error_code >= randr_error_base && error_code < randr_error_base + RANDR_ERROR_COUNT;
}

// Makes RandR's own errors on the window's X display a logged warning instead of the end of the app. Every other X
// error goes to the handler that was there before (SDL's, then Xlib's), as it did, and ends the app. Does nothing where the window isn't an
// X11 one (Wayland, macOS, Windows) or the build has no Xlib. Call it once the window exists.
void install_x11_error_handler(SDL_Window* window);

}

#endif
