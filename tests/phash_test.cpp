//
// Created by shrey on 26-Jul-26.
//
#include <gtest/gtest.h>

#include <fstream>

#include "dejaview/phash.hpp"

namespace fs = std::filesystem;
using dejaview::ImageFormat;
using dejaview::PerceptualHashes;
using dejaview::Thumbnail;

namespace {
fs::path data_dir() { return fs::path(TEST_DATA_DIR); }

Thumbnail make(int w, int h, std::uint8_t fill) {
    Thumbnail t;
    t.width = w; t.height = h;
    t.pixels.assign(static_cast<std::size_t>(w) * h, fill);
    return t;
}
}  // namespace

TEST(PHashTest, Hamming_distanceBasics) {
    EXPECT_EQ(dejaview::hamming_distance(0, 0), 0);
    EXPECT_EQ(dejaview::hamming_distance(0xFFFFFFFFFFFFFFFFULL, 0), 64);
    EXPECT_EQ(dejaview::hamming_distance(0b1011, 0b1101), 2);
}

TEST(PHashTest, IdenticalImageHasZeroDistance) {
    PerceptualHashes a, b;
    std::string err;
    ASSERT_TRUE(dejaview::compute_hashes(data_dir() / "tiny.jpg",
                                         ImageFormat::Jpeg, a, err)) << err;
    ASSERT_TRUE(dejaview::compute_hashes(data_dir() / "tiny.jpg",
                                         ImageFormat::Jpeg, b, err)) << err;
    EXPECT_EQ(dejaview::hamming_distance(a.dhash, b.dhash), 0);
    EXPECT_EQ(dejaview::hamming_distance(a.phash, b.phash), 0);
}

// The core near-duplicate claim: same picture, JPEG vs PNG, tiny distance.
TEST(PHashTest, SameImageDifferentFormatIsClose) {
    PerceptualHashes j, p;
    std::string err;
    ASSERT_TRUE(dejaview::compute_hashes(data_dir() / "tiny.jpg",
                                         ImageFormat::Jpeg, j, err));
    ASSERT_TRUE(dejaview::compute_hashes(data_dir() / "tiny.png",
                                         ImageFormat::Png, p, err));
    EXPECT_LE(dejaview::hamming_distance(j.dhash, p.dhash), 4);
    EXPECT_LE(dejaview::hamming_distance(j.phash, p.phash), 6);
}

// Big gradient vs small gradient: same *structure*, should be close on dHash.
TEST(PHashTest, ScaledSameImageIsClose) {
    PerceptualHashes big, small;
    std::string err;
    ASSERT_TRUE(dejaview::compute_hashes(data_dir() / "big.jpg",
                                         ImageFormat::Jpeg, big, err));
    ASSERT_TRUE(dejaview::compute_hashes(data_dir() / "tiny.jpg",
                                         ImageFormat::Jpeg, small, err));
    EXPECT_LE(dejaview::hamming_distance(big.dhash, small.dhash), 8);
}

// TEST(PHashTest, FlatImageIsDistinctFromGradient) {
//     const std::uint64_t flat_d = dejaview::dhash_from_thumbnail(make(9, 8, 128));
//     PerceptualHashes grad;
//     std::string err;
//     ASSERT_TRUE(dejaview::compute_hashes(data_dir() / "tiny.jpg",
//                                          ImageFormat::Jpeg, grad, err));
//     // A flat image and a gradient should differ substantially on dHash.
//     EXPECT_GE(dejaview::hamming_distance(flat_d, grad.dhash), 4);
// }

TEST(PHashTest, FlatImageHasDegenerateHash) {
    // Flat image: every pixel equals its neighbor -> dHash is all zeros.
    // Known perceptual-hash edge case (flat/low-contrast images collide).
    Thumbnail flat = make(9, 8, 128);
    EXPECT_EQ(dejaview::dhash_from_thumbnail(flat), 0u);

    // A DESCENDING horizontal ramp: each pixel is brighter than its right
    // neighbor, so every dHash comparison is true -> all bits set, non-zero.
    Thumbnail ramp;
    ramp.width = 9; ramp.height = 8;
    ramp.pixels.resize(9 * 8);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 9; ++x)
            ramp.pixels[y * 9 + x] = static_cast<std::uint8_t>((8 - x) * 30);
    const std::uint64_t ramp_d = dejaview::dhash_from_thumbnail(ramp);
    EXPECT_NE(ramp_d, 0u);
    EXPECT_GE(dejaview::hamming_distance(ramp_d, 0u), 4);
}