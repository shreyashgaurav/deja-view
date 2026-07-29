//
// Created by shrey on 29-Jul-26.
//

#include <gtest/gtest.h>

#include "dejaview/features.hpp"

namespace fs = std::filesystem;
using dejaview::ImageFeatures;
using dejaview::ImageFormat;
using dejaview::PairFeatures;

namespace {
fs::path data_dir() { return fs::path(TEST_DATA_DIR); }

std::array<float, dejaview::kHistSize> one_hot(std::size_t bin) {
    std::array<float, dejaview::kHistSize> h{};
    h[bin] = 1.0f;
    return h;
}
}  // namespace

TEST(FeaturesTest, IdenticalHistogramsHaveZeroDistance) {
    const auto h = one_hot(5);
    EXPECT_FLOAT_EQ(dejaview::histogram_distance(h, h), 0.0f);
}

TEST(FeaturesTest, DisjointHistogramsHaveMaxDistance) {
    EXPECT_FLOAT_EQ(dejaview::histogram_distance(one_hot(0), one_hot(63)), 1.0f);
}

TEST(FeaturesTest, PartialOverlapIsIntermediate) {
    std::array<float, dejaview::kHistSize> a{}, b{};
    a[0] = 0.5f; a[1] = 0.5f;
    b[1] = 0.5f; b[2] = 0.5f;
    EXPECT_NEAR(dejaview::histogram_distance(a, b), 0.5f, 1e-5);
}

TEST(FeaturesTest, ImageFeaturesReportSourceDimensions) {
    ImageFeatures f;
    std::string err;
    ASSERT_TRUE(dejaview::compute_image_features(data_dir() / "big.jpg",
                                                 ImageFormat::Jpeg, 12345, f, err))
        << err;
    EXPECT_EQ(f.width, 800);     // original size, not the 32x32 thumbnail
    EXPECT_EQ(f.height, 600);
    EXPECT_EQ(f.file_size, 12345u);
}

TEST(FeaturesTest, HistogramIsNormalised) {
    ImageFeatures f;
    std::string err;
    ASSERT_TRUE(dejaview::compute_image_features(data_dir() / "tiny.jpg",
                                                 ImageFormat::Jpeg, 100, f, err));
    float sum = 0.0f;
    for (float bin : f.histogram) sum += bin;
    EXPECT_NEAR(sum, 1.0f, 1e-4);
}

TEST(FeaturesTest, GradientHasNonZeroContrast) {
    ImageFeatures f;
    std::string err;
    ASSERT_TRUE(dejaview::compute_image_features(data_dir() / "big.jpg",
                                                 ImageFormat::Jpeg, 100, f, err));
    EXPECT_GT(f.contrast, 20.0f);  // a full black-to-white ramp
}

TEST(FeaturesTest, SameImageGivesNeutralPairFeatures) {
    ImageFeatures f;
    std::string err;
    ASSERT_TRUE(dejaview::compute_image_features(data_dir() / "tiny.jpg",
                                                 ImageFormat::Jpeg, 500, f, err));
    dejaview::CandidatePair p;  // all hash distances zero
    const auto pf = dejaview::compute_pair_features(p, f, f);

    EXPECT_FLOAT_EQ(pf.dim_ratio, 1.0f);
    EXPECT_FLOAT_EQ(pf.size_ratio, 1.0f);
    EXPECT_NEAR(pf.aspect_delta, 0.0f, 1e-5);
    EXPECT_NEAR(pf.hist_distance, 0.0f, 1e-5);
    EXPECT_NEAR(pf.brightness_delta, 0.0f, 1e-5);
}

TEST(FeaturesTest, FeatureVectorAndNamesAgree) {
    PairFeatures f;
    EXPECT_EQ(f.to_array().size(), PairFeatures::names().size());
    EXPECT_EQ(f.to_array().size(), PairFeatures::kCount);
}