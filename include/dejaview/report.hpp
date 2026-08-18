//
// Created by shrey on 18-Aug-26.
//
//Making Json so that the UI can make useof this.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include "dejaview/cluster.hpp"
#include "dejaview/exact_dedup.hpp"
#include "dejaview/features.hpp"
#include "dejaview/scanner.hpp"
namespace dejaview {

    //Everything the report needs, gathered from the pipeline stages.
    struct ReportInput {
        //Scan level
        std::vector<std::string> roots;
        std::size_t entries_visited = 0;
        std::size_t images_found = 0;
        std::size_t skipped_non_image = 0;
        std::uintmax_t total_bytes = 0;
        std::vector<ScanError> errors;

        //Per image data, parallel arrays indexed the same way as "clusters"
        const std::vector<FileEntry>* files = nullptr;
        const std::vector<std::size_t>* hashed_index = nullptr;
        const std::vector<ImageFeatures>* features = nullptr;

        //Stage results
        const ExactDedupResult* exact = nullptr;
        const ClusterResult* clusters = nullptr;
        const std::vector<ScoredPair>* scored_pairs = nullptr;
        const std::vector<PairFeatures>* pair_features = nullptr;  // parallel to scored_pairs
        //Timings and config
        double scan_ms = 0;
        double hash_ms = 0;
        double match_ms = 0;
        std::size_t candidates = 0;
        float threshold = 0.5f;
        bool model_loaded = false;
    };
    //Writes the canonical JSON report.
    bool write_report(const std::filesystem::path& out, const ReportInput& in,
                      std::string& error);

}