//
// Created by shrey on 31-Jul-26.
//

#include <gtest/gtest.h>

#include <algorithm>

#include "dejaview/cluster.hpp"

using dejaview::Cluster;
using dejaview::ClusterOptions;
using dejaview::ImageFeatures;
using dejaview::ScoredPair;

namespace {
bool has_member(const Cluster& c, std::size_t i) {
    return std::find(c.members.begin(), c.members.end(), i) != c.members.end();
}
}  // namespace

TEST(ClusterTest, StrongChainFormsOneGroup) {
    // 0~1 and 1~2, both confident: all three belong together.
    const std::vector<ScoredPair> pairs = {{0, 1, 0.99f}, {1, 2, 0.98f}};
    const auto r = dejaview::cluster_pairs(pairs, 5);

    ASSERT_EQ(r.clusters.size(), 1u);
    EXPECT_EQ(r.clusters[0].members.size(), 3u);
    EXPECT_NEAR(r.clusters[0].weakest_link, 0.98f, 1e-5);
}

TEST(ClusterTest, WeakEdgeCannotBridgeTwoGroups) {
    // Two solid pairs, joined only by a weak edge between them.
    const std::vector<ScoredPair> pairs = {
        {0, 1, 0.99f},   // group A
        {2, 3, 0.99f},   // group B
        {1, 2, 0.55f},   // weak bridge - must be refused
    };
    const auto r = dejaview::cluster_pairs(pairs, 6);

    ASSERT_EQ(r.clusters.size(), 2u);
    EXPECT_EQ(r.weak_merges_blocked, 1u);
    for (const auto& c : r.clusters) EXPECT_EQ(c.members.size(), 2u);
}

TEST(ClusterTest, WeakEdgeMayAttachASingleton) {
    // A lone item joining an established group on weak evidence IS allowed.
    const std::vector<ScoredPair> pairs = {{0, 1, 0.99f}, {1, 2, 0.55f}};
    const auto r = dejaview::cluster_pairs(pairs, 4);

    ASSERT_EQ(r.clusters.size(), 1u);
    EXPECT_EQ(r.clusters[0].members.size(), 3u);
    EXPECT_NEAR(r.clusters[0].weakest_link, 0.55f, 1e-5);  // cohesion recorded
    EXPECT_EQ(r.weak_merges_blocked, 0u);
}

TEST(ClusterTest, SingletonsAreNotReported) {
    const std::vector<ScoredPair> pairs = {{0, 1, 0.99f}};
    const auto r = dejaview::cluster_pairs(pairs, 10);
    ASSERT_EQ(r.clusters.size(), 1u);   // items 2..9 are not groups
}

TEST(ClusterTest, MaxSizeCapBlocksOversizedMerges) {
    const std::vector<ScoredPair> pairs = {
        {0, 1, 0.99f}, {1, 2, 0.99f}, {2, 3, 0.99f}};
    ClusterOptions opts;
    opts.max_cluster_size = 2;
    const auto r = dejaview::cluster_pairs(pairs, 5, opts);

    EXPECT_GT(r.size_merges_blocked, 0u);
    for (const auto& c : r.clusters) EXPECT_LE(c.members.size(), 2u);
}

TEST(ClusterTest, ClustersSortedLargestFirst) {
    const std::vector<ScoredPair> pairs = {
        {0, 1, 0.99f},                                  // group of 2
        {2, 3, 0.99f}, {3, 4, 0.99f}, {4, 5, 0.99f},    // group of 4
    };
    const auto r = dejaview::cluster_pairs(pairs, 8);
    ASSERT_EQ(r.clusters.size(), 2u);
    EXPECT_EQ(r.clusters[0].members.size(), 4u);
    EXPECT_EQ(r.clusters[1].members.size(), 2u);
}

TEST(ClusterTest, KeepRecommendationPrefersHigherResolution) {
    std::vector<ImageFeatures> feats(3);
    feats[0].width = 800;  feats[0].height = 600; feats[0].file_size = 500000;
    feats[1].width = 1920; feats[1].height = 1080; feats[1].file_size = 100;
    feats[2].width = 400;  feats[2].height = 300; feats[2].file_size = 900000;

    EXPECT_EQ(dejaview::recommend_keep({0, 1, 2}, feats), 1u);
}

TEST(ClusterTest, KeepRecommendationBreaksTiesByFileSize) {
    std::vector<ImageFeatures> feats(2);
    feats[0].width = 800; feats[0].height = 600; feats[0].file_size = 100000;
    feats[1].width = 800; feats[1].height = 600; feats[1].file_size = 250000;

    EXPECT_EQ(dejaview::recommend_keep({0, 1}, feats), 1u);
}