//
// Created by shrey on 30-Jul-26.
//


#include <gtest/gtest.h>

#include <cmath>
#include <fstream>

#include "dejaview/model.hpp"

namespace fs = std::filesystem;
using dejaview::PairClassifier;
using dejaview::PairFeatures;

namespace {

// Writes a weights file with the given feature-name list, all-zero means,
// unit scales, and the supplied coefficients.
fs::path write_model(const std::string& name,
                     const std::vector<std::string>& feature_names,
                     const std::vector<double>& coef, double intercept) {
    const fs::path p = fs::temp_directory_path() / name;
    std::ofstream out(p);
    out << "{\n  \"schema_version\": 1,\n"
        << "  \"model\": \"logistic_regression\",\n"
        << "  \"feature_names\": [";
    for (std::size_t i = 0; i < feature_names.size(); ++i) {
        out << (i ? ", " : "") << '"' << feature_names[i] << '"';
    }
    out << "],\n  \"mean\": [";
    for (std::size_t i = 0; i < coef.size(); ++i) out << (i ? ", " : "") << "0.0";
    out << "],\n  \"scale\": [";
    for (std::size_t i = 0; i < coef.size(); ++i) out << (i ? ", " : "") << "1.0";
    out << "],\n  \"coef\": [";
    for (std::size_t i = 0; i < coef.size(); ++i) out << (i ? ", " : "") << coef[i];
    out << "],\n  \"intercept\": " << intercept
        << ",\n  \"threshold\": 0.5\n}\n";
    return p;
}

std::vector<std::string> our_names() {
    std::vector<std::string> v;
    for (const char* n : PairFeatures::names()) v.emplace_back(n);
    return v;
}

}  // namespace

TEST(ModelTest, ZeroWeightsGiveHalfProbability) {
    const auto p = write_model("dv_zero.json", our_names(),
                               std::vector<double>(PairFeatures::kCount, 0.0), 0.0);
    PairClassifier m;
    std::string err;
    ASSERT_TRUE(dejaview::load_classifier(p, m, err)) << err;

    PairFeatures f;  // all zeros
    EXPECT_NEAR(m.probability(f), 0.5f, 1e-5);  // sigmoid(0) = 0.5
    fs::remove(p);
}

TEST(ModelTest, InterceptShiftsProbability) {
    const auto p = write_model("dv_inter.json", our_names(),
                               std::vector<double>(PairFeatures::kCount, 0.0), 2.0);
    PairClassifier m;
    std::string err;
    ASSERT_TRUE(dejaview::load_classifier(p, m, err)) << err;

    PairFeatures f;
    EXPECT_NEAR(m.probability(f), 1.0f / (1.0f + std::exp(-2.0f)), 1e-5);
    fs::remove(p);
}

TEST(ModelTest, LargeHashDistanceLowersProbability) {
    // Only d_dhash weighted, negatively - mirrors what training produces.
    std::vector<double> coef(PairFeatures::kCount, 0.0);
    coef[1] = -1.0;  // d_dhash
    const auto p = write_model("dv_dhash.json", our_names(), coef, 0.0);
    PairClassifier m;
    std::string err;
    ASSERT_TRUE(dejaview::load_classifier(p, m, err)) << err;

    PairFeatures near, far;
    near.d_dhash = 0.0f;
    far.d_dhash = 30.0f;
    EXPECT_GT(m.probability(near), m.probability(far));
    EXPECT_TRUE(m.is_duplicate(near));
    EXPECT_FALSE(m.is_duplicate(far));
    fs::remove(p);
}

TEST(ModelTest, FeatureNameMismatchIsRejected) {
    auto names = our_names();
    names[3] = "some_renamed_feature";
    const auto p = write_model("dv_bad.json", names,
                               std::vector<double>(PairFeatures::kCount, 0.0), 0.0);
    PairClassifier m;
    std::string err;
    EXPECT_FALSE(dejaview::load_classifier(p, m, err));
    EXPECT_NE(err.find("feature mismatch"), std::string::npos);
    fs::remove(p);
}

TEST(ModelTest, MissingFileFailsCleanly) {
    PairClassifier m;
    std::string err;
    EXPECT_FALSE(dejaview::load_classifier("/definitely/not/here.json", m, err));
    EXPECT_FALSE(err.empty());
    EXPECT_FALSE(m.loaded);
}

TEST(ModelTest, MalformedJsonFailsCleanly) {
    const fs::path p = fs::temp_directory_path() / "dv_broken.json";
    { std::ofstream(p) << "{ this is not json"; }
    PairClassifier m;
    std::string err;
    EXPECT_FALSE(dejaview::load_classifier(p, m, err));
    fs::remove(p);
}