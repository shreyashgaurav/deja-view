//
// Created by shrey on 24-Jul-26.
//

#include "dejaview/exact_dedup.hpp"
#include<unordered_map>
#include "dejaview/hashing.hpp"
namespace dejaview {
//Input: All scanned files (FileEntry objects)
//Output: An ExactDedupResult struct containing duplicate groups, errors and stats
ExactDedupResult find_exact_duplicates(const std::vector<FileEntry> &files) {
    ExactDedupResult result; //Accumulator. Returned at last

    //---- Funnel Step 1: Bucket bys size ---
    std::unordered_map<std :: uintmax_t, std::vector<std::size_t>> by_size; //Grouping files by size
    //Building the map
    for (std::size_t i = 0; i < files.size(); ++i) {
        by_size[files[i].size_bytes].push_back(i); //Storing only the index of the
    }

    //Now, we process only interesting size groups
    for (const auto& [size, indices] : by_size) {
        if (indices.size() < 2) continue; //Only one file with that size exists. That is unique as of now. So skip

        //---- Funnel step 2: hash the files in this bucket
        //Now, we hash every candidate.
        //by_hash is a map of: hash -> list of indices
        std :: unordered_map<std::uint64_t, std::vector<std::size_t>> by_hash;
        for (std :: size_t idx : indices) {
            std :: uint64_t h = 0;
            std :: string err;
            if (!hash_file(files[idx].path, h, err)) { //Hashing of files[idx].path: Stored in h
                result.errors.push_back({files[idx].path, err});
                continue;
            }
            ++result.files_hashed;
            result.bytes_hashed += size;
            by_hash[h].push_back(idx); //Populating the by_hash map. Ie, grouping files with identical hashes
        }

        // ---- Funnel Steo 3: byte-verify within each hash bucket
        //Now, we examine each hash bucket
        for (const auto& [hash, candidates] : by_hash) {
            if (candidates.size() < 2) continue; //Only one file with tahat hash. So, no chance of duplicacy
            //Verifying bye-by-byte (Expensive part)
            std::vector<std::vector<std::size_t>> verified; // Stores group of files that are verified to be identical byte by byte
            for (std :: size_t idx : candidates) {
                bool placed = false;
                for (auto& group : verified) {
                    bool same = false;
                    std :: string err;
                    if (!files_identical(files[group.front()].path, files[idx].path, same, err)) {
                        result.errors.push_back({files[idx].path, err});
                        break;
                    }
                    if (same) {
                        group.push_back(idx);
                        placed = true;
                        break;
                    }
                }
                if (!placed) verified.push_back({idx});
            }
            //Finding the space that could be freed up. If n files are identical then: size of 1 file * (n-1)
            for (auto& group : verified) {
                if (group.size() >= 2) {
                    result.reclaimable_bytes += static_cast<std :: uintmax_t>(group.size() - 1) * size;
                    result.groups.push_back(std::move(group));
                }
            }
        }
    }
    return result;
    }
}