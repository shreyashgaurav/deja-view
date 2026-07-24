#include<cstdio> //For printf
#include<dejaview/scanner.hpp> //scan_directories, ScanResult, FileEntry, ImageFormat
#include<dejaview/version.hpp> //For versioning
#include "dejaview/exact_dedup.hpp" //Duplicate finding : Filter 1 (Same size => FNV-1a)
#include "dejaview/decoder.hpp"
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

    // Stage 3: decode all images to thumbnails
    const auto t0 = std::chrono::steady_clock::now();
    std::size_t decoded = 0, failed = 0;
    for (const auto& f : result.files) {
        dejaview::Thumbnail thumb;
        std::string err;
        if (dejaview::decode_to_thumbnail(f.path, f.format, 32, 32, thumb, err)) { //32 x 32 thumbnails
            ++decoded;
        } else {
            ++failed;
        }
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0).count();
    std::printf("\nDecoded %zu thumbnails (%zu failed) in %lld ms  (%.1f images/s)\n",
                decoded, failed, static_cast<long long>(ms),
                ms > 0 ? decoded * 1000.0 / ms : 0.0);

    return 0;
}
