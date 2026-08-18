//
// Created by shrey on 18-Aug-26.
//


#include <gtest/gtest.h>

#include <fstream>

#include <nlohmann/json.hpp>

#include "dejaview/report.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

dejaview::FileEntry make_file(const char* path, std::uintmax_t bytes) {
    dejaview::FileEntry fe;
    fe.path = path;
    fe.size_bytes = bytes;
    fe.format = dejaview::ImageFormat::Jpeg;
    return fe;
}

dejaview::ImageFeatures make_feat(int w, int h, std::uintmax_t bytes) {
    dejaview::ImageFeatures f;
    f.width = w;
    f.height = h;
    f.file_size = bytes;
    return f;
}

}  // namespace

TEST(ReportTest, WritesValidJsonWithSchemaVersion) {
    const std::vector<dejaview::FileEntry> files = {
        make_file("/photos/big.jpg", 900000),
        make_file("/photos/small.jpg", 100000)};
    const std::vector<std::size_t> hashed = {0, 1};
    const std::vector<dejaview::ImageFeatures> feats = {
        make_feat(1920, 1080, 900000), make_feat(640, 480, 100000)};

    dejaview::ClusterResult clusters;
    dejaview::Cluster c;
    c.members = {0, 1};
    c.weakest_link = 0.97f;
    clusters.clusters.push_back(c);

    const std::vector<dejaview::ScoredPair> scored = {{0, 1, 0.97f}};
    dejaview::PairFeatures pf;
    pf.d_dhash = 3.0f;
    pf.hist_distance = 0.04f;
    const std::vector<dejaview::PairFeatures> pfs = {pf};

    dejaview::ReportInput in;
    in.roots = {"/photos"};
    in.images_found = 2;
    in.files = &files;
    in.hashed_index = &hashed;
    in.features = &feats;
    in.clusters = &clusters;
    in.scored_pairs = &scored;
    in.pair_features = &pfs;
    in.threshold = 0.5f;
    in.model_loaded = true;

    const fs::path out = fs::temp_directory_path() / "dv_report.json";
    std::string err;
    ASSERT_TRUE(dejaview::write_report(out, in, err)) << err;

    std::ifstream f(out);
    json j;
    ASSERT_NO_THROW(f >> j);

    EXPECT_EQ(j["schema_version"], 1);
    EXPECT_EQ(j["scan_info"]["images_found"], 2);
    EXPECT_TRUE(j["scan_info"]["classifier"]["loaded"]);
    ASSERT_EQ(j["groups"].size(), 1u);

    const auto& g = j["groups"][0];
    EXPECT_EQ(g["type"], "near");
    EXPECT_EQ(g["members"].size(), 2u);
    EXPECT_EQ(g["keep"], "/photos/big.jpg");          // higher resolution wins
    EXPECT_EQ(g["reclaimable_bytes"], 100000);        // the smaller copy
    ASSERT_EQ(g["pairs"].size(), 1u);
    EXPECT_NEAR(g["pairs"][0]["probability"].get<float>(), 0.97f, 1e-5);
    EXPECT_NEAR(g["pairs"][0]["features"]["d_dhash"].get<float>(), 3.0f, 1e-5);

    fs::remove(out);
}

TEST(ReportTest, MissingInputIsAnErrorNotACrash) {
    dejaview::ReportInput in;   // nothing populated
    std::string err;
    EXPECT_FALSE(dejaview::write_report("/tmp/dv_never.json", in, err));
    EXPECT_FALSE(err.empty());
}

TEST(ReportTest, MarksExactlyOneKeepPerGroup) {
    const std::vector<dejaview::FileEntry> files = {
        make_file("/a.jpg", 100), make_file("/b.jpg", 200),
        make_file("/c.jpg", 300)};
    const std::vector<std::size_t> hashed = {0, 1, 2};
    const std::vector<dejaview::ImageFeatures> feats = {
        make_feat(800, 600, 100), make_feat(800, 600, 200),
        make_feat(800, 600, 300)};

    dejaview::ClusterResult clusters;
    dejaview::Cluster c;
    c.members = {0, 1, 2};
    clusters.clusters.push_back(c);

    dejaview::ReportInput in;
    in.files = &files;
    in.hashed_index = &hashed;
    in.features = &feats;
    in.clusters = &clusters;

    const fs::path out = fs::temp_directory_path() / "dv_keep.json";
    std::string err;
    ASSERT_TRUE(dejaview::write_report(out, in, err)) << err;

    std::ifstream f(out);
    json j;
    f >> j;
    int keeps = 0;
    for (const auto& m : j["groups"][0]["members"]) {
        if (m["keep"].get<bool>()) ++keeps;
    }
    EXPECT_EQ(keeps, 1);
    fs::remove(out);
}