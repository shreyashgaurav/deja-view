//
// Created by shrey on 29-Jul-26.
//


#pragma once
#include <cstddef>
#include <filesystem>
#include <string>

namespace dejaview {

    struct ExportStats {
        std::size_t pairs_read = 0;
        std::size_t pairs_written = 0;
        std::size_t unique_images = 0;
        std::size_t images_failed = 0;
        std::size_t pairs_skipped = 0;//a pair whose image failed to decode
    };

    // Read a pair manifest- > compute features for every pair using the code the scanner uses, and write a training CSV.
    //`limit` of 0 means process the whole manifest.
    bool export_features(const std::filesystem::path& manifest,
                         const std::filesystem::path& out_csv, std::size_t limit,
                         ExportStats& stats, std::string& error);

}  // namespace dejaview