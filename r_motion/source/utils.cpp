
#include "r_motion/utils.h"
#include "r_utils/r_file.h"
#include <cmath>

using namespace std;
using namespace r_utils;
using namespace r_motion;

// r_motion TODO
// - add unit tests
//    - how to unit test motion detection?
//       - 1) simply run on consecutive frames and detect motion (motion value should be in specific range)
//       
// - implement morphological dilation and erosion

// https://homepages.inf.ed.ac.uk/rbf/HIPR2/dilate.htm#:~:text=The%20dilation%20operator%20takes%20two,dilation%20on%20the%20input%20image.

#if 0
// grayscale image, binary mask
void morph(inImage, outImage, kernel, type) {
 // half size of the kernel, kernel size is n*n (easier if n is odd)
 sz = (kernel.n - 1 ) / 2;

 for X in inImage.rows {
  for Y in inImage.cols {

   if ( isOnBoundary(X,Y, inImage, sz) ) {
    // check if pixel (X,Y) for boundary cases and deal with it (copy pixel as is)
    // must consider half size of the kernel
    val = inImage(X,Y);       // quick fix
   }

   else {
    list = [];

    // get the neighborhood of this pixel (X,Y)
    for I in kernel.n {
     for J in kernel.n {
      if ( kernel(I,J) == 1 ) {
       list.add( inImage(X+I-sz, Y+J-sz) );
      }
     }
    }

    if type == dilation {
     // dilation: set to one if any 1 is present, zero otherwise
     val = max(list);
    } else if type == erosion {
     // erosion: set to zero if any 0 is present, one otherwise
     val = min(list);
    }
   }

   // set output image pixel
   outImage(X,Y) = val;
  }
 }
}
#endif

r_image r_motion::argb_to_gray8(const r_image& argb)
{
    const uint8_t* src = argb.data->data();
    auto result = make_shared<vector<uint8_t>>(argb.width * argb.height);
    uint8_t* dst = result->data();

    for(uint16_t y = 0; y < argb.height; ++y)
    {
        for(uint16_t x = 0; x < argb.width; ++x)
        {
            ++src;
            uint32_t sum = *src;
            ++src;
            sum += *src;
            ++src;
            sum += *src;
            ++src;

            *dst = sum / 3;

            ++dst;
        }
    }

    r_image output;
    output.type = R_MOTION_IMAGE_TYPE_GRAY8;
    output.width = argb.width;
    output.height = argb.height;
    output.data = result;
    return output;
}

r_image r_motion::argb_to_gray16(const r_image& argb)
{
    const uint8_t* src = argb.data->data();
    auto result = make_shared<vector<uint8_t>>((argb.width*2) * argb.height);
    uint8_t* dst = result->data();

    for(uint16_t y = 0; y < argb.height; ++y)
    {
        for(uint16_t x = 0; x < argb.width; ++x)
        {
            ++src;
            uint32_t sum = (uint16_t)((((double)(*src)) / 255.0) * 65535.0);
            ++src;
            sum += (uint16_t)((((double)(*src)) / 255.0) * 65535.0);
            ++src;
            sum += (uint16_t)((((double)(*src)) / 255.0) * 65535.0);
            ++src;

            *(uint16_t*)dst = sum / 3;

            dst+=2;
        }
    }

    r_image output;
    output.type = R_MOTION_IMAGE_TYPE_GRAY16;
    output.width = argb.width;
    output.height = argb.height;
    output.data = result;
    return output;
}

r_image r_motion::gray8_to_argb(const r_image& gray)
{
    const uint8_t* src = gray.data->data();
    auto result = make_shared<vector<uint8_t>>((gray.width*4) * gray.height);
    uint8_t* dst = result->data();

    for(uint16_t y = 0; y < gray.height; ++y)
    {
        for(uint16_t x = 0; x < gray.width; ++x)
        {
            *dst = 255;
            ++dst;
            *dst = *src;
            ++dst;
            *dst = *src;
            ++dst;
            *dst = *src;
            ++dst;

            ++src;
        }
    }

    r_image output;
    output.type = R_MOTION_IMAGE_TYPE_ARGB;
    output.width = gray.width;
    output.height = gray.height;
    output.data = result;
    return output;
}

r_image r_motion::gray16_to_argb(const r_image& gray)
{
    const uint8_t* src = gray.data->data();
    auto result = make_shared<vector<uint8_t>>((gray.width*4) * gray.height);
    uint8_t* dst = result->data();

    for(uint16_t y = 0; y < gray.height; ++y)
    {
        for(uint16_t x = 0; x < gray.width; ++x)
        {
            uint16_t src_value = *(uint16_t*)src;

            uint8_t mv = (uint8_t)((((double)src_value) / 65535.0) * 255.0);

            *dst = 255;
            ++dst;
            *dst = mv;
            ++dst;
            *dst = mv;
            ++dst;
            *dst = mv;
            ++dst;

            src+=2;
        }
    }

    r_image output;
    output.type = R_MOTION_IMAGE_TYPE_ARGB;
    output.width = gray.width;
    output.height = gray.height;
    output.data = result;
    return output;
}

r_image r_motion::gray8_subtract(const r_image& a, const r_image& b)
{ 
    const uint8_t* src_a = a.data->data();
    const uint8_t* src_b = b.data->data();

    auto result = make_shared<vector<uint8_t>>(a.width * a.height);
    uint8_t* dst = result->data();

    for(uint16_t h = 0; h < a.height; ++h)
    {
        for(uint16_t w = 0; w < a.width; ++w)
        {
            uint8_t diff = std::abs(*src_a - *src_b);
            *dst = (diff>32)?diff:0;
            ++src_a;
            ++src_b;
            ++dst;
        }
    }

    r_image output;
    output.type = R_MOTION_IMAGE_TYPE_GRAY8;
    output.width = a.width;
    output.height = a.height;
    output.data = result;

    return output;
}

r_image r_motion::gray8_remove(const r_image& a, const r_image& b)
{
    auto output_buffer = make_shared<vector<uint8_t>>(a.width * a.height);

    for(uint16_t h = 0; h < a.height; ++h)
    {
        for(uint16_t w = 0; w < a.width; ++w)
        {
            if(a.data->data()[(h*a.width) + w] > 0)
            {
                if(b.data->data()[(h*b.width) + w] == 0)
                    output_buffer->data()[(h*a.width) + w] = a.data->data()[(h*a.width) + w];
                else
                    output_buffer->data()[(h*a.width) + w] = 0;
            }
        }
    }

    r_image output;
    output.type = R_MOTION_IMAGE_TYPE_GRAY8;
    output.width = a.width;
    output.height = a.height;
    output.data = output_buffer;

    return output;
}

r_image r_motion::average_images(const std::deque<r_image>& images)
{
    // Optimization Ideas:
    // - Implement r_image.type specific versions of this function
    //    - no innermost loop required
    //    - for ARGB, no reason to average the alpha channel, just skip it

    if(images.empty())
        R_THROW(("No images to average"));

    uint8_t src_pixel_size = 0;
    if(images.front().type == R_MOTION_IMAGE_TYPE_ARGB)
        src_pixel_size = 4;
    else if(images.front().type == R_MOTION_IMAGE_TYPE_GRAY8)
        src_pixel_size = 1;
    else if(images.front().type == R_MOTION_IMAGE_TYPE_GRAY16)
        src_pixel_size = 2;
    else R_THROW(("Unsupported image type"));

    uint16_t width = images.front().width;
    uint16_t height = images.front().height;

    auto result = make_shared<vector<uint8_t>>((width*src_pixel_size) * height);
    uint8_t* dst = result->data();

    for(uint16_t h = 0; h < height; ++h)
    {
        for(uint16_t w = 0; w < width; ++w)
        {
            for(int i = 0; i < src_pixel_size; ++i)
            {
                uint8_t sum = 0;
                for(const auto& image : images)
                {
                    sum += image.data->data()[(h*width*src_pixel_size) + (w*src_pixel_size) + i];
                }
                sum /= (uint8_t)images.size();

                *dst = sum;
                ++dst;
            }
        }
    }

    r_image output;
    output.type = images.front().type;
    output.width = width;
    output.height = height;
    output.data = result;
    return output;
}

uint64_t r_motion::gray8_compute_motion(const r_image& a)
{
    uint64_t sum = 0;

    for(uint16_t h = 0; h < a.height; ++h)
    {
        for(uint16_t w = 0; w < a.width; ++w)
        {
            uint8_t diff = a.data->data()[(h*a.width) + w];
            sum += diff;
        }
    }

    return sum;
}

void r_motion::ppm_write_argb(const std::string& filename, const r_image& image)
{
    auto outFile = r_file::open(filename, "w+b");

    fprintf(outFile, "P6\n%d %d\n255\n", image.width, image.height);

    const uint8_t* src = image.data->data();

    for(uint16_t y = 0; y < image.height; ++y)
    {
        for(uint16_t x = 0; x < image.width; ++x)
        {
            ++src;

            fwrite(src, 1, 1, outFile);
            ++src;
            fwrite(src, 1, 1, outFile);
            ++src;
            fwrite(src, 1, 1, outFile);
            ++src;
        }
    }

    outFile.close();
}
