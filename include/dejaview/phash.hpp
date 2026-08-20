//
// Created by shrey on 26-Jul-26.
//
#pragma once //Prevents for this header file to be included multiple times
#include <cstdint> //For uint64_t
#include <filesystem> //for getting paths
#include <string> //For erro rmessage

#include "dejaview/decoder.hpp" //imports Thumbnail, ImageFormat from decoder.hpp

namespace dejaview {

    // The three perceptual fingerprints of one image. Each is a 64-bit hash;
    // together they form the hash-distance features for the stage-6 classifier.
    struct PerceptualHashes { //This structure stores three different hashes
        std::uint64_t ahash = 0;  // average hash: brightness vs. mean
        std::uint64_t dhash = 0;  // difference hash: left-vs-right gradient
        std::uint64_t phash = 0;  // perceptual hash: low-frequency DCT structure

        //dHash of - mirrored, rotated 90/180/270. dHash alone is enough here:
        //it is the most reliable of the three, and carrying variants of all
        // three would triple the storage for little extra recall. So using dhash only
        std::uint64_t dhash_mirror = 0;
        std::uint64_t dhash_rot90 = 0;
        std::uint64_t dhash_rot180 = 0;
        std::uint64_t dhash_rot270 = 0;

    }; //Each of these three hashes are 64-bit and each algorithm captures a different visual property.

    // Hashes are computed directly from an already-decoded thumbnail.
    // (Thumbnail must be the correct size for the hash; see .cpp for details.)
    std::uint64_t ahash_from_thumbnail(const Thumbnail& t);  // expects 8x8
    std::uint64_t dhash_from_thumbnail(const Thumbnail& t); //expects 9x8
    std::uint64_t phash_from_thumbnail(const Thumbnail& t);// expects 32x32
    //Why different thumbnail sizes?
    /*
    | Hash            | Thumbnail size | Why                                                     |
    | --------------- | -------------- | ------------------------------------------------------- |
    | Average hash    | 8×8            | Needs 64 pixels → 64 bits                               |
    | Difference hash | 9×8            | Needs 64 left/right comparisons - 9 col => 8 comparisons|
    | Perceptual hash | 32×32          | Needs enough detail (larger image) for a DCT            |
    */

    // This func decode the image at each required size (8x8, 9x8, 32x32) and hash it
    // Returns false with `error` set if the image can't be decoded.
    bool compute_hashes(const std::filesystem::path& p, ImageFormat format,
                        PerceptualHashes& out, std::string& error);


    // Smallest dHash distance across every geometric variant of `a`. Equals the
    // plain dHash distance when neither image is transformed.
    int dhash_distance_any_orientation(const PerceptualHashes& a, const PerceptualHashes& b);

    // Hamming distance: number of differing bits. 0 = identical fingerprint,
    // 64 = maximally different. This is the core similarity function.
    // Inline because we call this many times. Declaring inline reduce function calling overhead => improve performance
    inline int hamming_distance(std::uint64_t a, std::uint64_t b) {
        return std::popcount(a ^ b);
    }

}  // namespace dejaview