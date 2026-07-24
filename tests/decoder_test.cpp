//
// Created by shrey on 24-Jul-26.
//
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>

#include "dejaview/decoder.hpp"

namespace fs = std::filesystem;
using dejaview::ImageFormat;
using dejaview::Thumbnail;

namespace {
fs::path data_dir() { return fs::path(TEST_DATA_DIR); }
}  // namespace

TEST(DecoderTest, JpegGradientDecodesWithCorrectOrientation) {
    Thumbnail t;
    std::string err;
    ASSERT_TRUE(dejaview::decode_to_thumbnail(data_dir() / "tiny.jpg",
                                              ImageFormat::Jpeg, 8, 8, t, err))
        << err;
    ASSERT_EQ(t.width, 8);
    ASSERT_EQ(t.height, 8);
    EXPECT_LT(t.at(0, 4), 40);    // left edge: dark
    EXPECT_GT(t.at(7, 4), 215);   // right edge: bright
    EXPECT_GT(t.at(7, 4) - t.at(0, 4), 150);  // strong left->right gradient
}

TEST(DecoderTest, PngGradientDecodesWithCorrectOrientation) {
    Thumbnail t;
    std::string err;
    ASSERT_TRUE(dejaview::decode_to_thumbnail(data_dir() / "tiny.png",
                                              ImageFormat::Png, 8, 8, t, err))
        << err;
    EXPECT_LT(t.at(0, 4), 40);
    EXPECT_GT(t.at(7, 4), 215);
}

// The near-duplicate thesis in miniature: same picture, different formats,
// thumbnails should be nearly identical.
TEST(DecoderTest, JpegAndPngOfSameImageProduceSimilarThumbnails) {
    Thumbnail j, p;
    std::string err;
    ASSERT_TRUE(dejaview::decode_to_thumbnail(data_dir() / "tiny.jpg",
                                              ImageFormat::Jpeg, 8, 8, j, err));
    ASSERT_TRUE(dejaview::decode_to_thumbnail(data_dir() / "tiny.png",
                                              ImageFormat::Png, 8, 8, p, err));
    int max_diff = 0;
    for (std::size_t i = 0; i < j.pixels.size(); ++i) {
        max_diff = std::max(max_diff,
                            std::abs(int(j.pixels[i]) - int(p.pixels[i])));
    }
    EXPECT_LT(max_diff, 25);  // JPEG is lossy, but only slightly at q92
}

TEST(DecoderTest, LargeJpegUsesScaledDecodePath) {
    Thumbnail t;
    std::string err;
    ASSERT_TRUE(dejaview::decode_to_thumbnail(data_dir() / "big.jpg",
                                              ImageFormat::Jpeg, 32, 32, t, err))
        << err;
    ASSERT_EQ(t.width, 32);
    EXPECT_LT(t.at(0, 16), 40);
    EXPECT_GT(t.at(31, 16), 215);
}

TEST(DecoderTest, CorruptJpegFailsGracefully) {
    const fs::path bad = fs::temp_directory_path() / "dejaview_bad.jpg";
    {
        std::ofstream out(bad, std::ios::binary);
        out.put('\xFF'); out.put('\xD8'); out.put('\xFF');  // valid magic...
        out << "this is definitely not jpeg data";           // ...garbage body
    }
    Thumbnail t;
    std::string err;
    EXPECT_FALSE(dejaview::decode_to_thumbnail(bad, ImageFormat::Jpeg, 8, 8,
                                               t, err));
    EXPECT_FALSE(err.empty());
    fs::remove(bad);
}