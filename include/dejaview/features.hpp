//
// Created by shrey on 29-Jul-26.
//
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

// #include "dejaview/decoder.hpp"
#include "dejaview/matcher.hpp"

namespace dejaview {

// 4 bins per channel -> 4*4*4 = 64-bin joint RGB histogram.
inline constexpr int kHistBinsPerChannel = 4;
inline constexpr int kHistSize = kHistBinsPerChannel * kHistBinsPerChannel *
                                 kHistBinsPerChannel;

// Computed once per image and reused for every pair that image appears in.
struct ImageFeatures {
    int width = 0; // original dimensions, not thumbnail
    int height = 0;
    std::uintmax_t file_size = 0;
    float mean_brightness = 0.0f; // 0..255
    float contrast = 0.0f; // luma std-dev
    std::array<float, kHistSize> histogram{};// normalised, sums to 1
};

bool compute_image_features(const std::filesystem::path& p, ImageFormat format,
                            std::uintmax_t file_size, ImageFeatures& out,
                            std::string& error);

// 0 = identical colour distribution, 1 = completely disjoint.
float histogram_distance(const std::array<float, kHistSize>& a,
                         const std::array<float, kHistSize>& b);

// The classifier's input vector. ORDER IS FIXED — the training CSV, the
// exported weights, and inference all depend on it matching exactly.
struct PairFeatures {
    float d_ahash = 0;
    float d_dhash = 0;
    float d_phash = 0;
    float dim_ratio = 0; // smaller pixel count / larger, 0..1
    float aspect_delta = 0;// |log(aspect_a / aspect_b)|
    float size_ratio = 0;// smaller file / larger, 0..1
    float hist_distance = 0; // 0..1
    float brightness_delta = 0; // 0..1
    float contrast_delta = 0; // 0..1
    float d_dhash_oriented = 0;   // appended: min distance across orientations


    static constexpr std::size_t kCount = 10;
    std::array<float, kCount> to_array() const;
    static std::array<const char*, kCount> names();
};

PairFeatures compute_pair_features(const CandidatePair& pair,
                                   const ImageFeatures& a,
                                   const ImageFeatures& b);

}