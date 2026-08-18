//
// Created by shrey on 31-Jul-26.
//

//Contract for stager 7

#pragma once
#include <cstddef>
#include <vector>

#include "dejaview/features.hpp"

namespace dejaview {

    //The "input" unit. Two image's index and their similarity prob
    struct ScoredPair {
        std::size_t a = 0;
        std::size_t b = 0;
        float probability = 0.0f;
    };

    struct ClusterOptions {
        //Pairs at or above this may merge two established groups.
        // Below it, a pair may only attach a lone item to a group - never bridge two groups.
        //This is what stops A~B~C chains from fusing dissimilar images.
        float core_probability = 0.9f;

        // Safety valve for pathological clusters. 0 = unlimited.
        std::size_t max_cluster_size = 0;
    };

    struct Cluster {
        std::vector<std::size_t> members;
        float weakest_link = 1.0f;  //lowest-confidence edge inside this group
    };

    struct ClusterResult {
        std::vector<Cluster> clusters;//only groups of 2 or more
        std::size_t weak_merges_blocked = 0;
        std::size_t size_merges_blocked = 0;
    };

    ClusterResult cluster_pairs(const std::vector<ScoredPair>& pairs,
                                std::size_t item_count,
                                const ClusterOptions& options = {});

    //Which file to keep. Highest pixel count, then largest file.
    // Returns an index into `members`' values/
    std::size_t recommend_keep(const std::vector<std::size_t>& members,
                               const std::vector<ImageFeatures>& features);

}