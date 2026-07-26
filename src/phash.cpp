//
// Created by shrey on 26-Jul-26.
//

//Takes the thumbnails produced by decoder.cpp and converts them to three fingerprints (a, d and p hashes)
#include "dejaview/phash.hpp" //corresponding header file

#include <algorithm> //Provides nth_element, that we use for finding median for pHash
#include <array> //Fixed size array. Unlike vector, its size is known at compile time
// #include <bit> //For pop-count ?
#include <cmath> //For cos(), sqrt()
#include <numeric> //For accumulatee
// #include <vector>

namespace dejaview {

// aHash
// Set bit i to 1 if pixel i is brighter than the image's average brightness.
std::uint64_t ahash_from_thumbnail(const Thumbnail& t) {
    const auto& px = t.pixels;  // Accessing 64 pixels for an 8x8 thumbnail
    const unsigned sum = std::accumulate(px.begin(), px.end(), 0u); //Total brightness
    const unsigned mean = sum / static_cast<unsigned>(px.size()); //Average brightness

    //Now, we build the aHash
    std::uint64_t hash = 0; //Initial hash
    for (std::size_t i = 0; i < px.size(); ++i) { //we visitinh every pixesl
        // If current px is > mean, we make the ith bit in 'hash' as 1, else keep it 0
        if (px[i] > mean) hash |= (std::uint64_t{1} << i);
    }
    return hash;
}

// dHash
// 9x8 input. Why 9? 9 cols gives 8 adjacent cols comparision
// For each of the 8 columns-pairs in each row, set a bit if the
// left pixel is brighter than its right neighbor. 8 rows x 8 comparisons = 64.
std::uint64_t dhash_from_thumbnail(const Thumbnail& t) {
    std::uint64_t hash = 0;
    int bit = 0; //Keeps track of which bit to set
    for (int y = 0; y < 8; ++y) { //col
        for (int x = 0; x < 8; ++x) { //row
            if (t.at(x, y) > t.at(x + 1, y)) {
                hash |= (std::uint64_t{1} << bit); //Set bth bit to 1
            }
            ++bit;
        }
    }
    return hash;
}

//pHash: is much more robust to compression, slight blurring, scaling, and small brightness changes.
namespace {
// Instead of looking directly at pixels, it analyzes the image in the frequency domain using a Discrete Cosine Transform (DCT).
//Which says: How quickly does brightness change across the image?
// 1-D DCT-II applied to build the 2-D transform.
// Precompute the 32x32 cosine coefficient matrix once (it never changes).
const std::array<std::array<float, 32>, 32>& dct_matrix() {
    //Lambda function
    static const auto mat = [] { //static beacuse we need to run only once, const because we nned to compute this only once
        std::array<std::array<float, 32>, 32> m{};
        for (int u = 0; u < 32; ++u) {
            const float cu = (u == 0) ? std::sqrt(1.0f / 32.0f)
                                      : std::sqrt(2.0f / 32.0f);
            for (int x = 0; x < 32; ++x) {
                m[u][x] = cu * std::cos((2 * x + 1) * u * 3.14159265358979f /
                                        (2 * 32));
            }
        }
        return m;
    }();
    return mat;
}

}

// 32x32 input. Take the 2-D DCT, keep the top-left 8x8 low-frequency block
// (skipping the [0][0] DC term, which is just overall brightness), threshold
// each against the median of that block.
std::uint64_t phash_from_thumbnail(const Thumbnail& t) {
    const auto& C = dct_matrix(); //DCT matrix

    // Load pixels as floats.
    std::array<std::array<float, 32>, 32> f{};
    //Convert pixels to floating point
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            f[y][x] = static_cast<float>(t.at(x, y));

    // Separable 2-D DCT: rows first, then columns. tmp = C * f^T style.
    std::array<std::array<float, 32>, 32> tmp{};
    for (int u = 0; u < 32; ++u)
        for (int x = 0; x < 32; ++x) {
            float s = 0.0f;
            for (int y = 0; y < 32; ++y) s += C[u][y] * f[y][x];
            tmp[u][x] = s;
        }
    std::array<std::array<float, 32>, 32> D{};
    for (int u = 0; u < 32; ++u)
        for (int v = 0; v < 32; ++v) {
            float s = 0.0f;
            for (int x = 0; x < 32; ++x) s += tmp[u][x] * C[v][x];
            D[u][v] = s;
        }

    // Collect the top-left 8x8 low-frequency coefficients (excluding DC).
    std::array<float, 64> block{};
    int n = 0;
    for (int u = 0; u < 8; ++u)
        for (int v = 0; v < 8; ++v) block[n++] = D[u][v];

    // Median of the 63 non-DC coefficients.
    std::array<float, 64> sorted = block;
    std::nth_element(sorted.begin() + 1, sorted.begin() + 32, sorted.end());
    const float median = sorted[32];


    //Now building the hash
    std::uint64_t hash = 0;
    for (int i = 1; i < 64; ++i)  // skip block[0] = DC term
        if (block[i] > median) hash |= (std::uint64_t{1} << i);
    return hash;
}

// Coordinates the decoder and the three hash functions
bool compute_hashes(const std::filesystem::path& p, ImageFormat format,
                    PerceptualHashes& out, std::string& error) {
    Thumbnail t8, t9, t32; //initally empty
    if (!decode_to_thumbnail(p, format, 8, 8, t8, error)) return false;
    if (!decode_to_thumbnail(p, format, 9, 8, t9, error)) return false;
    if (!decode_to_thumbnail(p, format, 32, 32, t32, error)) return false;
    out.ahash = ahash_from_thumbnail(t8);
    out.dhash = dhash_from_thumbnail(t9);
    out.phash = phash_from_thumbnail(t32);
    return true;
}
/*
 * compute_hashes() acts as the high-level driver that connects the decoder (decoder.cpp)
 * with the three hashing algorithms (aHash, dHash, and pHash) and packages the results into
 * a single PerceptualHashes structure.
*/
}  // namespace dejaview