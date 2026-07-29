//
// Created by shrey on 24-Jul-26.
//

#pragma once
#include <cstdio>
#include<filesystem>
#include<string>
#include<vector>

#include "dejaview/scanner.hpp" // For Image format
namespace dejaview {
    struct Thumbnail { //This struct represents a small greyscale version of an image
        int width = 0; //width of the thumbnail
        int height = 0; //height of the thumbnail
        std :: vector<std :: uint8_t>pixels; //stores all the pixels in Row-major, width*height + x

        std :: uint8_t at(int x, int y) const { //Return the pixel at co-ordinate
            return pixels[static_cast<std::size_t>(y) * width + x];
        }
    };

    // Small RGB image plus the source image's original dimensions.
    // Used for colour histograms and dimension-based pair features.
    struct ColorThumbnail {
        int width = 0;
        int height = 0;
        int source_width = 0;   // original image size, before any resizing
        int source_height = 0;
        std::vector<std::uint8_t> pixels;  // RGB interleaved, 3 * width * height

        std::size_t idx(int x, int y) const {
            return (static_cast<std::size_t>(y) * width + x) * 3;
        }
        std::uint8_t r(int x, int y) const { return pixels[idx(x, y)]; }
        std::uint8_t g(int x, int y) const { return pixels[idx(x, y) + 1]; }
        std::uint8_t b(int x, int y) const { return pixels[idx(x, y) + 2]; }
    };

    //Definition of a function that reads an image file and produce a thumbnail
    bool decode_to_thumbnail(const std::filesystem::path& p, ImageFormat format,
        int target_w, int target_h, Thumbnail& out,
        std::string& error);

    bool decode_to_color_thumbnail(const std::filesystem::path& p, ImageFormat format,
                                   int target_w, int target_h, ColorThumbnail& out,
                                   std::string& error);
}