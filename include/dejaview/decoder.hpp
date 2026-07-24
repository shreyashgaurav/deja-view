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
    //Definition of a function that reads an image file and produce a thumbnail
    bool decode_to_thumbnail(const std::filesystem::path& p, ImageFormat format,
        int target_w, int target_h, Thumbnail& out,
        std::string& error);
}