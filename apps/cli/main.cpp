#include<cstdio> //For printf
#include<dejaview/scanner.hpp> //scan_directories, ScanResult, FileEntry, ImageFormat
#include<dejaview/version.hpp> //For versioning
#include "dejaview/exact_dedup.hpp" //Duplicate finding : Filter 1 (Same size => FNV-1a)
#include "dejaview/decoder.hpp"
#include "dejaview/phash.hpp"
#include "dejaview/matcher.hpp"
#include "dejaview/features.hpp"
#include<chrono>

int main(int const argc, char** argv) { //Count of arguments, and arguments vector
    //argv[0]: program name aleways
    //argv[1...]: directory
    if (argc < 2) {
        std :: printf("Dejaview %s\nUsage: dejaview <directory> [More directories...]\n",
            dejaview::version());
        return 1;
    }

    //Which directories to scan
    std :: vector<std::filesystem::path> roots;
    for (int i = 1; i < argc; ++i){ //i = 1: because argv[0] is the program name itself
        roots.emplace_back(argv[i]); //Emplace is slightly more efficient than push_back: No temp obj is created
    }
    //Scan the directories for images anf store the result in result (type is ScanResult)
    const auto result = dejaview::scan_directories(roots);
    std::uintmax_t total_bytes = 0;
    std::size_t jpegs = 0, pngs = 0, mismatches = 0;
    //Loop over every image and get stats like: total bytes, count of jpegs, pngs and mismatched images
    for (const auto& f : result.files) {
        total_bytes += f.size_bytes;
        (f.format == dejaview::ImageFormat::Jpeg ? jpegs : pngs)++;
        if (f.extension_mismatch) ++mismatches;
    }
    //Print the stats
    std :: printf("Scanned %zu entries\n", result.entries_visited);
    std :: printf("Images found: %zu (%zu JPEG, %zu PNG)\n", result.files.size(), jpegs, pngs);
    std::printf("Total image data: %.1f MB\n", static_cast<double>(total_bytes) / (1024.0 * 1024.0));
    std :: printf("Non-image files skipped: %zu\n", result.skipped_non_image);
    if (mismatches) std :: printf("Extension mismatches: %zu\n", mismatches);

    //Print the errors for debugging
    if (!result.errors.empty()) {
        std :: printf("Errors: %zu (first: %s - %s)\n", result.errors.size(),
            result.errors.front().path.string().c_str(),
            result.errors.front().message.c_str());
    }

    const auto dupes = dejaview::find_exact_duplicates(result.files);
    std :: printf("\nExact duplicate groups: %zu (%zu files hashed, %.1f MB read)\n",
        dupes.groups.size(), dupes.files_hashed,
        static_cast<double>(dupes.bytes_hashed) / (1024.0 * 1024.0));
    std :: printf("Reclaimable Space: %.1f MB\n",
        static_cast<double>(dupes.reclaimable_bytes) / (1024.0 * 1024.0));

    for (const auto& group : dupes.groups) {
        std :: printf("  group of %zu:\n", group.size());
        for (std :: size_t idx : group) {
            std :: printf("    %s\n", result.files[idx].path.string().c_str());
        }
    }

    // Stage 3+4: decode + perceptual hashes
    const auto t0 = std::chrono::steady_clock::now(); //current time
    std::vector<dejaview::PerceptualHashes> hashes; //Storage for hashes for each image (3 hashes for each image)
    std::vector<std::size_t> hashed_index;  // maps into result.files. For cases in which some image fails to be decoded
    std::size_t failed = 0; //Counting failures
    for (std::size_t i = 0; i < result.files.size(); ++i) {
        dejaview::PerceptualHashes h; // Temp hash structure
        std::string err;
        //COmpute hashes
        if (dejaview::compute_hashes(result.files[i].path, result.files[i].format, h, err)) {
            hashes.push_back(h);
            hashed_index.push_back(i);
        } else {
            ++failed;
        }
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(); //Timer stops
    //Elapsed time
    std::printf("\nHashed %zu images (%zu failed) in %lld ms  (%.1f images/s)\n",
                hashes.size(), failed, static_cast<long long>(ms),
                ms > 0 ? hashes.size() * 1000.0 / ms : 0.0);

    // Quick brute-force near-duplicate peek (dHash distance <= 10).
    // This is a preview of stage 5, not the real matcher.
    //Will change this with a ML classifier
    //int near_pairs = 0;
    //for (std::size_t a = 0; a < hashes.size(); ++a)
    //    for (std::size_t b = a + 1; b < hashes.size(); ++b)
    //        if (dejaview::hamming_distance(hashes[a].dhash, hashes[b].dhash) <= 10)
    //            ++near_pairs;
    //std::printf("Near-duplicate pairs (dHash <= 10): %d\n", near_pairs);

    // Stage 5: candidate matchin
    const auto m0 = std::chrono::steady_clock::now();
    const auto match = dejaview::find_candidates(hashes);
    const auto m_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - m0).count();

    std::printf("\nCandidate pairs: %zu  (from %llu comparisons in %lld ms)\n",
                match.pairs.size(),
                static_cast<unsigned long long>(match.comparisons),
                static_cast<long long>(m_ms));
    std::printf("  before cap: %zu, images hitting cap: %zu\n",
                match.pairs_before_cap, match.images_capped);
    if (m_ms > 0) {
        std::printf("  throughput: %.1f M comparisons/s\n",
                    match.comparisons / (m_ms * 1000.0));
    }

    // Stage 6 (a) : per image features + pair features
    std::vector<dejaview::ImageFeatures> img_feats(hashed_index.size());
    std::size_t feat_failed = 0;
    for (std::size_t k = 0; k < hashed_index.size(); ++k) {
        const auto& fe = result.files[hashed_index[k]];
        std::string err;
        if (!dejaview::compute_image_features(fe.path, fe.format, fe.size_bytes,
                                              img_feats[k], err)) {
            ++feat_failed;
                                              }
    }

    int colour_agree = 0;
    for (const auto& p : match.pairs) {
        const auto pf = dejaview::compute_pair_features(p, img_feats[p.a],
                                                        img_feats[p.b]);
        if (pf.hist_distance < 0.15f) ++colour_agree;
    }
    std::printf("\nImage features computed (%zu failed)\n", feat_failed);
    std::printf("Candidates whose colours also agree (hist < 0.15): %d / %zu\n",
                colour_agree, match.pairs.size());

    return 0;
}
