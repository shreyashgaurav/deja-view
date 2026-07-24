//
// Created by shrey on 24-Jul-26.
//

#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "dejaview/scanner.hpp"
namespace dejaview {
    //stores everything produced by duplicate detection. Like a report.
    struct ExactDedupResult {
        std::vector<std::vector<std::size_t>> groups; //Duplicate groups
        std :: vector<ScanError> errors; //Storing error

        std :: size_t files_hashed = 0; //count hoe many files were sucessfully hashed
        std :: uintmax_t bytes_hashed = 0; //Tracs how much data we read. Will use this for progress reporting
        std :: uintmax_t reclaimable_bytes = 0; //how much space could be recovered if duplicate copies were removed
    };
    //Takes a list of scanned files, and returns an ExactDedupResult.
    ExactDedupResult find_exact_duplicates(const std::vector<FileEntry>& files);
}