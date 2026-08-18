#include<cstdio> //For printf
#include<string>
#include <fstream>
#include<algorithm>
#include<dejaview/scanner.hpp> //scan_directories, ScanResult, FileEntry, ImageFormat
#include<dejaview/version.hpp> //For versioning
#include "dejaview/exact_dedup.hpp" //Duplicate finding : Filter 1 (Same size => FNV-1a)
#include "dejaview/decoder.hpp"
#include "dejaview/phash.hpp"
#include "dejaview/matcher.hpp"
#include "dejaview/features.hpp"
#include "dejaview/export.hpp"
#include "dejaview/model.hpp"
#include "dejaview/cluster.hpp"
#include "dejaview/report.hpp"
#include<chrono>

int main(int const argc, char** argv) { //Count of arguments, and arguments vector
    //argv[0]: program name aleways
    //argv[1...]: directory

    // Subcommand dispatch. Default (no subcommand) stays "scan a directory",
    // so existing usage is unchanged.
    if (argc >= 4 && std::string(argv[1]) == "export-features") {
        const std::size_t limit = (argc >= 5)
                                      ? std::stoull(argv[4])
                                      : 0;
        dejaview::ExportStats stats;
        std::string err;
        if (!dejaview::export_features(argv[2], argv[3], limit, stats, err)) {
            std::printf("export failed: %s\n", err.c_str());
            return 1;
        }
        std::printf("pairs read:      %zu\n", stats.pairs_read);
        std::printf("unique images:   %zu\n", stats.unique_images);
        std::printf("images failed:   %zu\n", stats.images_failed);
        std::printf("pairs skipped:   %zu\n", stats.pairs_skipped);
        std::printf("pairs written:   %zu\n", stats.pairs_written);
        return 0;
    }



    // Mine hard negatives: pairs of DIFFERENT images that the hashes think
    // are similar. Since this directory holds only originals (no derivatives),
    // every pair found here is a true negative by construction.
    if (argc >= 4 && std::string(argv[1]) == "mine-negatives") {
        const int radius = (argc >= 5) ? std::stoi(argv[4]) : 16;

        const auto scan = dejaview::scan_directories({argv[2]});
        std::printf("found %zu images in %s\n", scan.files.size(), argv[2]);

        std::vector<dejaview::PerceptualHashes> hashes;
        std::vector<std::size_t> idx;
        for (std::size_t i = 0; i < scan.files.size(); ++i) {
            if (i % 500 == 0) {
                std::printf("\r  hashing %zu/%zu", i, scan.files.size());
                std::fflush(stdout);
            }
            dejaview::PerceptualHashes h;
            std::string err;
            if (dejaview::compute_hashes(scan.files[i].path,
                                         scan.files[i].format, h, err)) {
                hashes.push_back(h);
                idx.push_back(i);
            }
        }
        std::printf("\r  hashed %zu images        \n", hashes.size());

        dejaview::MatchOptions opts;
        opts.ahash_radius = radius;
        opts.dhash_radius = radius;
        opts.phash_radius = radius;
        opts.max_candidates_per_image = 0;   // no cap: we want every one

        const auto match = dejaview::find_candidates(hashes, opts);

        std::ofstream out(argv[3]);
        if (!out) {
            std::printf("cannot write %s\n", argv[3]);
            return 1;
        }
        out << "path_a,path_b,d_ahash,d_dhash,d_phash\n";
        for (const auto& p : match.pairs) {
            out << scan.files[idx[p.a]].path.string() << ','
                << scan.files[idx[p.b]].path.string() << ','
                << p.d_ahash << ',' << p.d_dhash << ',' << p.d_phash << '\n';
        }
        std::printf("wrote %zu candidate hard negatives to %s\n",
                    match.pairs.size(), argv[3]);
        return 0;
    }



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

    // Stage 6e:: classify candidate pairs
    dejaview::PairClassifier clf;
    std::string model_err;
    const char* model_path = "training/model/weights.json";
    const bool have_model = dejaview::load_classifier(model_path, clf, model_err);
    if (!have_model) {
        std::printf("\nWARNING: no classifier (%s) - falling back to a pHash "
                    "threshold. Results will be worse.\n", model_err.c_str());
    }

    // Dump every judged pair for inspection. This is how real hard negatives
    // get found: look at what the model got wrong on data it never trained on.
    std::ofstream dump("classified.csv");
    dump << "probability,verdict,path_a,path_b";
    for (const char* n : dejaview::PairFeatures::names()) dump << ',' << n;
    dump << '\n';

    int duplicates = 0, rejected = 0;
    int bucket[10] = {0};
    std::vector<dejaview::ScoredPair> scored;
    std::vector<dejaview::PairFeatures> scored_feats;
    float highest = 0.0f;
    std::size_t best_a = 0, best_b = 0;

    for (const auto& pr : match.pairs) {
        const auto pf = dejaview::compute_pair_features(pr, img_feats[pr.a],
                                                        img_feats[pr.b]);
        bool dup;
        float prob = 0.0f;
        if (have_model) {
            prob = clf.probability(pf);
            dup = prob >= clf.threshold;
        } else {
            dup = pr.d_phash <= 8;   // degraded fallback (SRS ML-I2)
        }

        bucket[std::min(9, static_cast<int>(prob * 10))]++;

        // Paths are quoted: filenames can contain commas.
        dump << prob << ',' << (dup ? 1 : 0) << ",\""
             << result.files[hashed_index[pr.a]].path.string() << "\",\""
             << result.files[hashed_index[pr.b]].path.string() << '"';
        for (float v : pf.to_array()) dump << ',' << v;
        dump << '\n';

        if (dup) {
            ++duplicates;
            scored.push_back({pr.a, pr.b, prob});
            scored_feats.push_back(pf);
            if (prob > highest) {
                highest = prob;
                best_a = pr.a;
                best_b = pr.b;
            }
        } else {
            ++rejected;
        }
    }

    std::printf("\n=== Classification ===\n");
    std::printf("  candidates:  %zu\n", match.pairs.size());
    std::printf("  duplicates:  %d\n", duplicates);
    std::printf("  rejected:    %d  (%.1f%% of candidates)\n", rejected,
                match.pairs.empty() ? 0.0
                    : 100.0 * rejected / match.pairs.size());

    std::printf("\n  probability distribution:\n");
    for (int i = 0; i < 10; ++i) {
        std::printf("    %.1f-%.1f : %d\n", i / 10.0, (i + 1) / 10.0, bucket[i]);
    }

    if (have_model && duplicates > 0) {
        std::printf("\n  most confident: p=%.4f\n    %s\n    %s\n", highest,
                    result.files[hashed_index[best_a]].path.string().c_str(),
                    result.files[hashed_index[best_b]].path.string().c_str());
    }

    // Stage 7: clustering the imagess
    const auto clusters = dejaview::cluster_pairs(scored, hashes.size());

    std::uintmax_t reclaimable = 0;
    for (const auto& c : clusters.clusters) {
        const std::size_t keep = dejaview::recommend_keep(c.members, img_feats);
        for (std::size_t m : c.members) {
            if (m != keep) reclaimable += img_feats[m].file_size;
        }
    }

    std::printf("\nGroups\n");
    std::printf("  duplicate groups: %zu  (from %zu pairs)\n",
                clusters.clusters.size(), scored.size());
    std::printf("  weak bridges refused: %zu\n", clusters.weak_merges_blocked);
    std::printf("  reclaimable: %.1f MB\n",
                static_cast<double>(reclaimable) / (1024.0 * 1024.0));

    const std::size_t show = std::min<std::size_t>(5, clusters.clusters.size());
    for (std::size_t i = 0; i < show; ++i) {
        const auto& c = clusters.clusters[i];
        const std::size_t keep = dejaview::recommend_keep(c.members, img_feats);
        std::printf("\n  group %zu: %zu files (weakest link p=%.3f)\n",
                    i + 1, c.members.size(), c.weakest_link);
        for (std::size_t m : c.members) {
            std::printf("    %s %s\n", m == keep ? "KEEP  " : "      ",
                        result.files[hashed_index[m]].path.string().c_str());
        }
    }



    //Stage 8 JSON report => Wll be used by UI
    dejaview::ReportInput ri;
    for (int i = 1; i < argc; ++i) ri.roots.push_back(argv[i]);
    ri.entries_visited = result.entries_visited;
    ri.images_found = result.files.size();
    ri.skipped_non_image = result.skipped_non_image;
    ri.errors = result.errors;
    ri.files = &result.files;
    ri.hashed_index = &hashed_index;
    ri.features = &img_feats;
    ri.exact = &dupes;
    ri.clusters = &clusters;
    ri.scored_pairs = &scored;
    ri.pair_features = &scored_feats;
    ri.candidates = match.pairs.size();
    ri.threshold = have_model ? clf.threshold : 0.5f;
    ri.model_loaded = have_model;
    for (const auto& f : result.files) ri.total_bytes += f.size_bytes;

    std::string report_err;
    if (dejaview::write_report("dejaview-report.json", ri, report_err)) {
        std::printf("\nreport written to dejaview-report.json\n");
    } else {
        std::printf("\nreport failed: %s\n", report_err.c_str());
    }

    return 0;
}
