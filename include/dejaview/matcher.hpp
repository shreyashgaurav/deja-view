//
// Created by shrey on 27-Jul-26.
//

#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <algorithm>

#include "dejaview/phash.hpp"

namespace dejaview {
    //This struct stores a pair worth examining
    //The three distances are carried forward as the ML claddifier features
    struct CandidatePair {
        std::size_t a = 0; //Index of image 1 in the hashes vector
        std::size_t b = 0; //Index of image 2 in the hashes vector
        int d_ahash = 0; //Stores hamming distance between average, dHash dist and pHash dist
        int d_dhash = 0;
        int d_phash = 0;
        int d_dhash_oriented = 0; // min dHash distance across orientations
        // How close on the best-agreeing hash - used for ranking under the cap.
        int best_distance() const {
            return std::min({d_ahash, d_dhash, d_phash});
        }
    };
    //Setting the matching algorithm

    struct MatchOptions {
        // A pair qualifies if ANY hash is within its radius (union, not
        // intersection): each hash is blind to different edits, so the union
        // maximizes recall. Over-inclusion is intentional at this stage. Later we ll use ML classifier for a fine grained classifier

        //Why "ANY "? : The pair should survive even when only one hash agrees
        int ahash_radius = 16;
        int dhash_radius = 16;
        int phash_radius = 16;

        // Safety valve: keep at most this many candidates per image (its closest).
        // A pair survives if it is in the top-N of EITHER endpoint. 0 = unlimited.
        std::size_t max_candidates_per_image = 50;
    };

    //Stores candidatePairs
    struct MatchResult {
        std::vector<CandidatePair> pairs;
        std::uint32_t comparisons = 0; //Pairs that are examined
        std::size_t images_capped = 0; //image that hit the cap
        std::size_t pairs_before_cap = 0;
    };
    // MatchResult{
    //     std::vector<CandidatePair> pairs;
    //     std::uint64_t comparisons = 0;
    //     std::size_t image_capped = 0;
    //     std::size_t pairs_before_cap = 0;
    // }

    //Input: All images hashes. Output: candidate pairs. The param matchOptions& oprions = {} is for default MatchOption
    MatchResult find_candidates(const std::vector<PerceptualHashes>& hashes, const MatchOptions& options = {});
}
