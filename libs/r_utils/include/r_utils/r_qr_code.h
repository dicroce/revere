
#ifndef r_utils_r_qr_code_h
#define r_utils_r_qr_code_h

#include "r_utils/r_macro.h"
#include <string>
#include <vector>

namespace r_utils
{

namespace r_qr_code
{

// A QR code as a size x size grid of modules, row-major, true = dark. It has
// no quiet zone: leave at least 4 light modules around it when drawing.
struct qr_code
{
    int size {0};
    std::vector<bool> modules;

    bool dark(int x, int y) const { return modules[(size_t)y * size + x]; }
};

// Encodes text (e.g. a URL) at medium error correction. Returns an empty code
// (size 0) if the text is too long for any QR version.
R_API qr_code encode(const std::string& text);

}

}

#endif
