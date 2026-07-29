//
// Created by shrey on 29-Jul-26.
//

#include <gtest/gtest.h>

#include "dejaview/matcher.hpp"

using dejaview::CandidatePair;
using dejaview::MatchOptions;
using dejaview::PerceptualHashes;

namespace {
PerceptualHashes make(std::uint64_t a, std::uint64_t d, std::uint64_t p) {
    PerceptualHashes h;
    h.ahash = a; h.dhash = d; h.phash = p;
    return h;
}
}  // namespace

TEST(MatcherTest, IdenticalHashesAreCandidates) {
    std::vector<PerceptualHashes> hs = {make(0xFF, 0xFF, 0xFF),
                                        make(0xFF, 0xFF, 0xFF)};
    const auto r = dejaview::find_candidates(hs);
    ASSERT_EQ(r.pairs.size(), 1u);
    EXPECT_EQ(r.pairs[0].d_dhash, 0);
    EXPECT_EQ(r.comparisons, 1u);
}

TEST(MatcherTest, DistantHashesAreNotCandidates) {
    std::vector<PerceptualHashes> hs = {
        make(0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL),
        make(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL)};
    const auto r = dejaview::find_candidates(hs);
    EXPECT_TRUE(r.pairs.empty());
    EXPECT_EQ(r.comparisons, 1u);
}

// Union semantics: one close hash is enough, even if the others are far.
TEST(MatcherTest, AnySingleCloseHashQualifiesThePair) {
    std::vector<PerceptualHashes> hs = {
        make(0x0ULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL),
        make(0x3ULL, 0x0000000000000000ULL, 0x0000000000000000ULL)};
    const auto r = dejaview::find_candidates(hs);
    ASSERT_EQ(r.pairs.size(), 1u);  // aHash distance 2 carries it
    EXPECT_EQ(r.pairs[0].d_ahash, 2);
    EXPECT_EQ(r.pairs[0].d_dhash, 64);
}

TEST(MatcherTest, ComparisonCountIsQuadratic) {
    std::vector<PerceptualHashes> hs(10, make(0, 0, 0));
    const auto r = dejaview::find_candidates(hs);
    EXPECT_EQ(r.comparisons, 45u);          // 10*9/2
    EXPECT_EQ(r.pairs.size(), 45u);         // all identical -> all candidates
}

TEST(MatcherTest, CapLimitsCandidatesPerImage) {
    std::vector<PerceptualHashes> hs(20, make(0, 0, 0));  // all identical
    MatchOptions opts;
    opts.max_candidates_per_image = 5;
    const auto r = dejaview::find_candidates(hs, opts);

    EXPECT_EQ(r.pairs_before_cap, 190u);     // 20*19/2 all qualify
    EXPECT_LT(r.pairs.size(), 190u);         // cap actually pruned
    EXPECT_GT(r.images_capped, 0u);
    // Union semantics: at most 20*5 kept slots, deduped.
    EXPECT_LE(r.pairs.size(), 100u);
}

TEST(MatcherTest, ZeroCapMeansUnlimited) {
    std::vector<PerceptualHashes> hs(20, make(0, 0, 0));
    MatchOptions opts;
    opts.max_candidates_per_image = 0;
    const auto r = dejaview::find_candidates(hs, opts);
    EXPECT_EQ(r.pairs.size(), 190u);
    EXPECT_EQ(r.images_capped, 0u);
}