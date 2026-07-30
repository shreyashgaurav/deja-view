//
// Created by shrey on 29-Jul-26.
//

#include "dejaview/export.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "dejaview/features.hpp"
#include "dejaview/matcher.hpp"
#include "dejaview/phash.hpp"
#include "dejaview/scanner.hpp"

namespace fs = std::filesystem;

namespace dejaview {
namespace {

struct PairRow {
    std::string path_a, path_b, transform, split, base_a, base_b;
    int label = 0;
};

//Minimal CSV split.
//The manifest is machine-generated with no quoted fields or embedded commas, so a full RFC-4180 parser would be over-engineering.

std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, ',')) {
        // Python's csv writer emits CRLF; getline only eats the LF, so the
        // last cell would keep a trailing '\r' and corrupt our output.
        if (!cell.empty() && cell.back() == '\r') cell.pop_back();
        out.push_back(cell);
    }
    return out;
}


//everything we need to know about one imag is computed exactly once.
struct ImageRecord {
    PerceptualHashes hashes;
    ImageFeatures features;
    bool ok = false;
};

}

bool export_features(const fs::path& manifest, const fs::path& out_csv,
                     std::size_t limit, ExportStats& stats, std::string& error) {
    std::ifstream in(manifest);
    if (!in) {
        error = "cannot open manifest: " + manifest.string();
        return false;
    }

    // First pass; read the manifest
    std::string line;
    if (!std::getline(in, line)) {
        error = "manifest is empty";
        return false;
    }
    //Header order from make_pairs.py:
    // path_a,path_b,label,transform,split,base_a,base_b
    std::vector<PairRow> pairs;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const auto cells = split_csv(line);
        if (cells.size() < 7) continue;
        PairRow r;
        r.path_a = cells[0];
        r.path_b = cells[1];
        r.label = std::stoi(cells[2]);
        r.transform = cells[3];
        r.split = cells[4];
        r.base_a = cells[5];
        r.base_b = cells[6];
        pairs.push_back(std::move(r));
        if (limit > 0 && pairs.size() >= limit) break;
    }
    stats.pairs_read = pairs.size();

    // Second Pass: compute per-image data once perimage
    // An image appearing in 50 pairs is decoded once, not 50 times.
    std::unordered_map<std::string, std::size_t> index_of;
    std::vector<std::string> paths;
    auto intern = [&](const std::string& p) {
        auto it = index_of.find(p);
        if (it != index_of.end()) return it->second;
        const std::size_t idx = paths.size();
        index_of.emplace(p, idx);
        paths.push_back(p);
        return idx;
    };
    for (const auto& r : pairs) {
        intern(r.path_a);
        intern(r.path_b);
    }
    stats.unique_images = paths.size();

    std::vector<ImageRecord> records(paths.size());
    for (std::size_t i = 0; i < paths.size(); ++i) {
        if (i % 500 == 0) {
            std::printf("\r  images %zu/%zu", i, paths.size());
            std::fflush(stdout);
        }
        const fs::path p(paths[i]);
        std::string err;

        ImageFormat format{};
        if (!detect_image_format(p, format, err)) {
            ++stats.images_failed;
            continue;
        }
        std::error_code ec;
        const auto size = fs::file_size(p, ec);
        if (ec) {
            ++stats.images_failed;
            continue;
        }
        if (!compute_hashes(p, format, records[i].hashes, err)) {
            ++stats.images_failed;
            continue;
        }
        if (!compute_image_features(p, format, size, records[i].features, err)) {
            ++stats.images_failed;
            continue;
        }
        records[i].ok = true;
    }
    std::printf("\r  images %zu/%zu\n", paths.size(), paths.size());

    //Third pass: Pair features + CSV
    std::ofstream out(out_csv);
    if (!out) {
        error = "cannot write: " + out_csv.string();
        return false;
    }

    out << "label,transform,split,base_a,base_b";
    for (const char* name : PairFeatures::names()) out << ',' << name;
    out << '\n';

    for (const auto& r : pairs) {
        const std::size_t ia = index_of[r.path_a];
        const std::size_t ib = index_of[r.path_b];
        if (!records[ia].ok || !records[ib].ok) {
            ++stats.pairs_skipped;
            continue;
        }

        //Build a CandidatePair by hand: the matcher isn't involved here, but the feature code expects the hash distances in this form.
        CandidatePair cp;
        cp.a = ia;
        cp.b = ib;
        cp.d_ahash = hamming_distance(records[ia].hashes.ahash,
                                      records[ib].hashes.ahash);
        cp.d_dhash = hamming_distance(records[ia].hashes.dhash,
                                      records[ib].hashes.dhash);
        cp.d_phash = hamming_distance(records[ia].hashes.phash,
                                      records[ib].hashes.phash);

        const PairFeatures pf = compute_pair_features(
            cp, records[ia].features, records[ib].features);

        out << r.label << ',' << r.transform << ',' << r.split << ','
            << r.base_a << ',' << r.base_b;
        for (float v : pf.to_array()) out << ',' << v;
        out << '\n';
        ++stats.pairs_written;
    }
    return true;
}

}