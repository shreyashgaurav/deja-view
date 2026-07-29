//
// Created by shrey on 29-Jul-26.
//

#include "dejaview/features.hpp"

#include <algorithm>
#include <cmath>

namespace dejaview {

bool compute_image_features(const std::filesystem::path& p, ImageFormat format,
                            std::uintmax_t file_size, ImageFeatures& out,
                            std::string& error) {
    // NOTE: this is an additional decode per image, on top of the three the
    // hashes already do. Unifying them (decode once, derive all) is on the
    // performance list — correctness first.
    ColorThumbnail t;
    if (!decode_to_color_thumbnail(p, format, 32, 32, t, error)) return false;

    out.width = t.source_width;
    out.height = t.source_height;
    out.file_size = file_size;
    out.histogram.fill(0.0f);

    const int n = t.width * t.height;
    double sum_luma = 0.0, sum_luma_sq = 0.0;

    for (int y = 0; y < t.height; ++y) {
        for (int x = 0; x < t.width; ++x) {
            const int r = t.r(x, y), g = t.g(x, y), b = t.b(x, y);

            // Bin index: top 2 bits of each channel (256 / 4 = 64 per bin).
            const int bin = (r >> 6) * 16 + (g >> 6) * 4 + (b >> 6);
            out.histogram[static_cast<std::size_t>(bin)] += 1.0f;

            const double luma = 0.299 * r + 0.587 * g + 0.114 * b;
            sum_luma += luma;
            sum_luma_sq += luma * luma;
        }
    }

    for (float& bin : out.histogram) bin /= static_cast<float>(n);

    const double mean = sum_luma / n;
    const double variance = std::max(0.0, sum_luma_sq / n - mean * mean);
    out.mean_brightness = static_cast<float>(mean);
    out.contrast = static_cast<float>(std::sqrt(variance));
    return true;
}

float histogram_distance(const std::array<float, kHistSize>& a,
                         const std::array<float, kHistSize>& b) {
    // Histogram intersection: sum of per-bin minimums. Both histograms sum to
    // 1, so the intersection is in [0,1]; distance is its complement.
    float intersection = 0.0f;
    for (std::size_t i = 0; i < kHistSize; ++i) {
        intersection += std::min(a[i], b[i]);
    }
    return std::clamp(1.0f - intersection, 0.0f, 1.0f);
}

std::array<float, PairFeatures::kCount> PairFeatures::to_array() const {
    return {d_ahash, d_dhash, d_phash, dim_ratio, aspect_delta,
            size_ratio, hist_distance, brightness_delta, contrast_delta};
}

std::array<const char*, PairFeatures::kCount> PairFeatures::names() {
    return {"d_ahash", "d_dhash", "d_phash", "dim_ratio", "aspect_delta",
            "size_ratio", "hist_distance", "brightness_delta", "contrast_delta"};
}

PairFeatures compute_pair_features(const CandidatePair& pair,
                                   const ImageFeatures& a,
                                   const ImageFeatures& b) {
    PairFeatures f;
    f.d_ahash = static_cast<float>(pair.d_ahash);
    f.d_dhash = static_cast<float>(pair.d_dhash);
    f.d_phash = static_cast<float>(pair.d_phash);

    const double px_a = std::max(1.0, double(a.width) * a.height);
    const double px_b = std::max(1.0, double(b.width) * b.height);
    f.dim_ratio = static_cast<float>(std::min(px_a, px_b) / std::max(px_a, px_b));

    const double asp_a = double(a.width) / std::max(1, a.height);
    const double asp_b = double(b.width) / std::max(1, b.height);
    f.aspect_delta = static_cast<float>(
        std::abs(std::log(std::max(1e-6, asp_a) / std::max(1e-6, asp_b))));

    const double sz_a = std::max<std::uintmax_t>(1, a.file_size);
    const double sz_b = std::max<std::uintmax_t>(1, b.file_size);
    f.size_ratio = static_cast<float>(std::min(sz_a, sz_b) / std::max(sz_a, sz_b));

    f.hist_distance = histogram_distance(a.histogram, b.histogram);
    f.brightness_delta =
        std::abs(a.mean_brightness - b.mean_brightness) / 255.0f;
    f.contrast_delta =
        std::clamp(std::abs(a.contrast - b.contrast) / 128.0f, 0.0f, 1.0f);
    return f;
}

}  // namespace dejaview