//
// Created by shrey on 24-Jul-26.
//

//Read a JPEG or PNg image -> convert it to grayscale -> riesize it to the thubnail size -> return the thumbnail
#include "dejaview/decoder.hpp"

#include <csetjmp>
#include <cstdio>

#include <jpeglib.h>
#include <png.h>

namespace fs = std::filesystem;

namespace dejaview {
namespace { //Functions in this anonymous namespace can't be called from other files

// ___________________JPEG

// libjpeg terminates the program if iit encounters a corrupt image.
// This is not good for our application.
// So, we make our custom error message for graceful recovery
// libjpeg reports fatal errors by calling error_exit, which must not return.
// We longjmp back to a recovery point instead of letting it call exit().

struct JpegErrorMgr {
    jpeg_error_mgr pub;   // must be first: libjpeg sees this as its error mgr
    std::jmp_buf jump; //this stores a recovery point
};

//This is a custom error function
//Whenever libjpeg encounters a fatal error, it calls this, instead of exiting
void jpeg_error_exit(j_common_ptr cinfo) {
    auto* mgr = reinterpret_cast<JpegErrorMgr*>(cinfo->err);
    std::longjmp(mgr->jump, 1); //control goes to setjmp(jerr.jump) in below function
}

/*
Jpeg and png are not raw pixels. They go through a variety of compressions.
For using the image for our task we need the raw pixels. so we need to reconstruct
Decoding is the process of converting a compressed image file (JPEG or PNG) into an
array of actual pixel values stored in memory.
*/

//This gives a greyscale version and a scaled down version of the image. Stored in the Thumbnail struct
bool decode_jpeg_gray(const fs::path& p, int target_w, int target_h,
                      Thumbnail& out, std::string& error) {
    //Using C style file I/O because libjpeg expects a FILE*
    FILE* f = std::fopen(p.string().c_str(), "rb");
    if (!f) { //If open fails
        error = "cannot open";
        return false;
    }

    // SAFETY RULES: every C++ object with a destructor is declared before
    // setjmp. longjmp must never jump over an object's construction.
    jpeg_decompress_struct cinfo{}; //libjpeg's main decoder object: stores image size, color space, decoder setting, current scanline
    JpegErrorMgr jerr{}; //Creates the custom error handler
    //Bellow two lines say: Use normal error message, But, if something fatal happens, call this function
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpeg_error_exit;

    if (setjmp(jerr.jump)) {
        // We land here if libjpeg hits a fatal error anywhere below.
        jpeg_destroy_decompress(&cinfo);
        std::fclose(f);
        error = "corrupt or unsupported JPEG";
        return false;
    }

    //Initializing decompression
    jpeg_create_decompress(&cinfo); //Allocate decoder resourses
    jpeg_stdio_src(&cinfo, f); //read from the file f
    jpeg_read_header(&cinfo, TRUE); //Read metadata like width, height, color format

    cinfo.out_color_space = JCS_GRAYSCALE;  // library converts color to grayscale

    // Scaled decode: largest 1/2^k (up to 1/8) that stays >= target size.
    //These two lines gives decoding factor = scale_num / scale_denom = 1(Initially)
    cinfo.scale_num = 1;
    cinfo.scale_denom = 1;
    //Now we keep increasing the denominator
    //Explanation of the conditions:
        // libjpeg supports: 1/1, 1/2, 1/4, 1/8 : So, we go till scale_denom < 8;
        // next two condition: checks if the shrink is at least as wide or long as given target w and h
        // so that when we exit, it becomes as close as possible to target_w and target_h
    while (cinfo.scale_denom < 8 &&
           cinfo.image_width / (cinfo.scale_denom * 2) >=
               static_cast<unsigned>(target_w) &&
           cinfo.image_height / (cinfo.scale_denom * 2) >=
               static_cast<unsigned>(target_h)) {
        cinfo.scale_denom *= 2;
    }

    // Uptill now, libjpeg has only read the JPEG header (metadata)
    //Now, the decompression starts
    jpeg_start_decompress(&cinfo); //start decompression. scaled down w and h are stored
    out.width = static_cast<int>(cinfo.output_width); // Get the scaled down width
    out.height = static_cast<int>(cinfo.output_height); //Get the scale down height
    out.pixels.resize(static_cast<std::size_t>(out.width) * out.height);
    //Pixels is a vector<uint8_t> => Every greyscale pixel uses one byte. So, Total pixels = w * h * 1 (As coded in the above line)

    //Decoding starts
    // scanlines means rows (initially zero)
    //The condition: 0 < output_height (total row count)
    while (cinfo.output_scanline < cinfo.output_height) { //Runs once for every row of the image
        JSAMPROW row = out.pixels.data() +
                       static_cast<std::size_t>(cinfo.output_scanline) * out.width; //This line computes where the next decoded row should be stored
        jpeg_read_scanlines(&cinfo, &row, 1); //ACTUAL DECODING HAPPENS HERE. This line says: Decode one scanline from the JPEG file and write it into the memory pointed to by row
        //What value is placed? libjpeg performs a scaled inverse DCT.
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    std::fclose(f);
    return true;
}
// TODO: Redundancy here in this function. Merge the above and below function in one in the optimization pass
// RGB variant of the above. Identical structure; the only real differences are
// JCS_RGB instead of JCS_GRAYSCALE, and 3 bytes per pixel instead of 1.
// We also record the ORIGINAL dimensions before the scaled decode shrinks them.
// TODO: unify the gray/rgb paths - the setjmp boilerplate is duplicated
bool decode_jpeg_rgb(const fs::path& p, int target_w, int target_h,
                     ColorThumbnail& out, std::string& error) {
    FILE* f = std::fopen(p.string().c_str(), "rb");
    if (!f) {
        error = "cannot open";
        return false;
    }

    jpeg_decompress_struct cinfo{};
    JpegErrorMgr jerr{};
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpeg_error_exit;

    if (setjmp(jerr.jump)) {
        jpeg_destroy_decompress(&cinfo);
        std::fclose(f);
        error = "corrupt or unsupported JPEG";
        return false;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, f);
    jpeg_read_header(&cinfo, TRUE);

    // Captured before scaled decode: these are the true source dimensions.
    // After jpeg_start_decompress, output_width is the SHRUNKEN size, so using
    // that would make every large JPEG look small to the dim_ratio feature.
    out.source_width = static_cast<int>(cinfo.image_width);
    out.source_height = static_cast<int>(cinfo.image_height);

    cinfo.out_color_space = JCS_RGB;   // 3 channels, not 1

    cinfo.scale_num = 1;
    cinfo.scale_denom = 1;
    while (cinfo.scale_denom < 8 &&
           cinfo.image_width / (cinfo.scale_denom * 2) >=
               static_cast<unsigned>(target_w) &&
           cinfo.image_height / (cinfo.scale_denom * 2) >=
               static_cast<unsigned>(target_h)) {
        cinfo.scale_denom *= 2;
    }

    jpeg_start_decompress(&cinfo);
    out.width = static_cast<int>(cinfo.output_width);
    out.height = static_cast<int>(cinfo.output_height);
    out.pixels.resize(static_cast<std::size_t>(out.width) * out.height * 3);

    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW row = out.pixels.data() +
                       static_cast<std::size_t>(cinfo.output_scanline) *
                           out.width * 3;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    std::fclose(f);
    return true;
}

// _______________ PNG
    //This function reads a PNG image from disk and converts it into a grayscale thumbnail
bool decode_png_gray(const fs::path& p, Thumbnail& out, std::string& error) {
    FILE* f = std::fopen(p.string().c_str(), "rb"); //Open the file in C style format bcz libpng requires that
    if (!f) { //If file opening fails
        error = "cannot open";
        return false;
    }

    //Two strutures are requires:
        //(1): decoder state:  Stores current file, error
        //(2): info: Stores metadata like width, height, color type, bit depth, interlace method
    png_structp png =
        png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    if (!png || !info) { //if any allocation fails
        if (png) png_destroy_read_struct(&png, &info, nullptr); //Cleaning up the png struct
        std::fclose(f);
        error = "libpng initialization failed";
        return false;
    }

    // Now we are doing the same this: continuing gracefully instead of exiting when a corrupted image is encountred
    // Same safety rule: vectors exist before setjmp.
    std::vector<std::uint8_t> pix;
    std::vector<png_bytep> rows;

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(f);
        error = "corrupt or unsupported PNG";
        return false;
    }

    png_init_io(png, f); //Tells libpng where to read from
    png_read_info(png, info); //read image header (metadata) - No pixels yet

    // Normalize every PNG flavor down to 8-bit single-channel grayscale.
    //PNG supports multiple bit depths. Ex: 16 Bit (values = [0, 65535]). But we need value = [0, 255] => 8 bits enough
    png_set_strip_16(png);        // 16-bit depth -> 8-bit
    //Png does not store RGB values directly. It store indeices that correspond to color pixels.
    png_set_expand(png);          // palette -> RGB, 1/2/4-bit gray -> 8-bit
    if (png_get_color_type(png, info) & PNG_COLOR_MASK_COLOR) {
        png_set_rgb_to_gray_fixed(png, 1, -1, -1);  // default luma weights
    }
    png_set_strip_alpha(png); //Remove alpha channel. Alpha layer act as mask controlling the opacity of the pixels
    png_read_update_info(png, info); //Everything before was just requested transformations. this line applies all transformations

    const int w = static_cast<int>(png_get_image_width(png, info)); //widht. png_get_image_width returns png_uint_32. So we typecast to int
    const int h = static_cast<int>(png_get_image_height(png, info)); //height
    if (png_get_channels(png, info) != 1) { //Only one channel should be there bcz we applied the transformations and image now is grayscale
        png_destroy_read_struct(&png, &info, nullptr);//cleanup
        std::fclose(f);
        error = "unexpected channel count after gray conversion";
        return false;
    }

    pix.resize(static_cast<std::size_t>(w) * h); //Allocate memory for all pixels
    rows.resize(static_cast<std::size_t>(h)); //Allocate row pointer. total = h
    for (int y = 0; y < h; ++y) {//Point each row ptr to its correct place
        rows[static_cast<std::size_t>(y)] =
            pix.data() + static_cast<std::size_t>(y) * w;
    }
    //ACTUAL DECODING Happens by the below line
    png_read_image(png, rows.data());  // handles interlaced PNGs internally
    png_read_end(png, nullptr);
    png_destroy_read_struct(&png, &info, nullptr);
    std::fclose(f);

    out.width = w;
    out.height = h;
    out.pixels = std::move(pix);
    return true;
}

// TODO: Redundancy here in this function too. Merge the above and below function in one in the optimization pass
// RGB variant. Note the inverted transformation: the gray path converts colour
// DOWN to gray; here we expand gray UP to rgb so every PNG ends as 3 channels.
bool decode_png_rgb(const fs::path& p, ColorThumbnail& out, std::string& error) {
    FILE* f = std::fopen(p.string().c_str(), "rb");
    if (!f) {
        error = "cannot open";
        return false;
    }

    png_structp png =
        png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    if (!png || !info) {
        if (png) png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(f);
        error = "libpng initialization failed";
        return false;
    }

    std::vector<std::uint8_t> pix;
    std::vector<png_bytep> rows;

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(f);
        error = "corrupt or unsupported PNG";
        return false;
    }

    png_init_io(png, f);
    png_read_info(png, info);

    png_set_strip_16(png);
    png_set_expand(png);
    png_set_strip_alpha(png);
    png_set_gray_to_rgb(png);   // grayscale PNGs become 3-channel too
    png_read_update_info(png, info);

    const int w = static_cast<int>(png_get_image_width(png, info));
    const int h = static_cast<int>(png_get_image_height(png, info));
    if (png_get_channels(png, info) != 3) {
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(f);
        error = "unexpected channel count after rgb conversion";
        return false;
    }

    pix.resize(static_cast<std::size_t>(w) * h * 3);
    rows.resize(static_cast<std::size_t>(h));
    for (int y = 0; y < h; ++y) {
        rows[static_cast<std::size_t>(y)] =
            pix.data() + static_cast<std::size_t>(y) * w * 3;
    }
    png_read_image(png, rows.data());
    png_read_end(png, nullptr);
    png_destroy_read_struct(&png, &info, nullptr);
    std::fclose(f);

    out.width = w;
    out.height = h;
    out.source_width = w;    // PNG has no scaled-decode, so these are the same
    out.source_height = h;
    out.pixels = std::move(pix);
    return true;
}

// _______________ resize ----
// Box filter: each target pixel = average of its source rectangle.
// Simple, fast, and exactly what perceptual hashing wants (area averaging).
void box_resize(const Thumbnail& src, int tw, int th, Thumbnail& out) {
    out.width = tw;
    out.height = th;
    out.pixels.assign(static_cast<std::size_t>(tw) * th, 0);

    for (int ty = 0; ty < th; ++ty) {
        int y0 = ty * src.height / th;
        int y1 = (ty + 1) * src.height / th;
        if (y1 <= y0) y1 = y0 + 1;
        for (int tx = 0; tx < tw; ++tx) {
            int x0 = tx * src.width / tw;
            int x1 = (tx + 1) * src.width / tw;
            if (x1 <= x0) x1 = x0 + 1;

            unsigned sum = 0;
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    sum += src.pixels[static_cast<std::size_t>(y) * src.width + x];
                }
            }
            out.pixels[static_cast<std::size_t>(ty) * tw + tx] =
                static_cast<std::uint8_t>(sum / static_cast<unsigned>(
                                                    (y1 - y0) * (x1 - x0)));
        }
    }
}

// TODO: Redundancy here in this function too. Merge the above and below function in one in the optimization pass
// Box filter for RGB: same area-averaging as above, done once per channel.
void box_resize_rgb(const ColorThumbnail& src, int tw, int th,
                    ColorThumbnail& out) {
    out.width = tw;
    out.height = th;
    out.source_width = src.source_width;    // carried through the resize
    out.source_height = src.source_height;
    out.pixels.assign(static_cast<std::size_t>(tw) * th * 3, 0);

    for (int ty = 0; ty < th; ++ty) {
        int y0 = ty * src.height / th;
        int y1 = (ty + 1) * src.height / th;
        if (y1 <= y0) y1 = y0 + 1;
        for (int tx = 0; tx < tw; ++tx) {
            int x0 = tx * src.width / tw;
            int x1 = (tx + 1) * src.width / tw;
            if (x1 <= x0) x1 = x0 + 1;
            const unsigned count = static_cast<unsigned>((y1 - y0) * (x1 - x0));

            for (int ch = 0; ch < 3; ++ch) {
                unsigned sum = 0;
                for (int y = y0; y < y1; ++y) {
                    for (int x = x0; x < x1; ++x) {
                        sum += src.pixels[
                            (static_cast<std::size_t>(y) * src.width + x) * 3 + ch];
                    }
                }
                out.pixels[(static_cast<std::size_t>(ty) * tw + tx) * 3 + ch] =
                    static_cast<std::uint8_t>(sum / count);
            }
        }
    }
}

}  // namespace


//Decoding Orchestrator: Doe not decode itself. But decides which decoder to use
bool decode_to_thumbnail(const fs::path& p, const ImageFormat format, const int target_w,
                         const int target_h, Thumbnail& out, std::string& error) {
    Thumbnail full; //Temporary thumbnail that holds the decoded image

    //Decode according to the image format
    const bool ok = (format == ImageFormat::Jpeg)
                        ? decode_jpeg_gray(p, target_w, target_h, full, error)
                        : decode_png_gray(p, full, error);
    if (!ok) return false;

    //Do we have the desired size?
    if (full.width == target_w && full.height == target_h) { //Yes, we have.
        out = std::move(full);
    } else {  //No, we dont't
        box_resize(full, target_w, target_h, out);
    }
    return true;
}

//Colour orchestrator: same dispatch logic, RGB output
bool decode_to_color_thumbnail(const fs::path& p, const ImageFormat format,
                               const int target_w, const int target_h,
                               ColorThumbnail& out, std::string& error) {
    ColorThumbnail full;

    const bool ok = (format == ImageFormat::Jpeg)
                        ? decode_jpeg_rgb(p, target_w, target_h, full, error)
                        : decode_png_rgb(p, full, error);
    if (!ok) return false;

    if (full.width == target_w && full.height == target_h) {
        out = std::move(full);
    } else {
        box_resize_rgb(full, target_w, target_h, out);
    }
    return true;
}

}


