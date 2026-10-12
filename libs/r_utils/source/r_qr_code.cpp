
#include "r_utils/r_qr_code.h"
#include "r_utils/3rdparty/qrcodegen/qrcodegen.hpp"

using namespace std;
using namespace r_utils;

r_qr_code::qr_code r_qr_code::encode(const string& text)
{
    qr_code out;
    try
    {
        auto qr = qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        out.size = qr.getSize();
        out.modules.resize((size_t)out.size * out.size);
        for(int y = 0; y < out.size; ++y)
            for(int x = 0; x < out.size; ++x)
                out.modules[(size_t)y * out.size + x] = qr.getModule(x, y);
    }
    catch(const qrcodegen::data_too_long&)
    {
        out = qr_code();
    }
    return out;
}
