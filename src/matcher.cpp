//
// Created by shrey on 27-Jul-26.
//

#include "dejaview/matcher.hpp"
#include<algorithm>
#include<queue>

namespace dejaview {
namespace {
    struct WorstFirst { //Comparator
        bool operator()(const CandidatePair& x, const CandidatePair& y) const {
            return x.best_distance() < y.best_distance();
        }
    };
    //alias
    using BoundedHeap = std::priority_queue<CandidatePair, std::vector<CandidatePair>, WorstFirst>;

    //(2, 7) == (7, 2). This does that:
    std::pair<std::size_t, std::size_t> key_of(const CandidatePair& p) {
        return p.a < p.b ? std :: make_pair(p.a, p.b) : std::make_pair(p.b, p.a);
    }
}

MatchResult find_candidates(const std::vector<PerceptualHashes>& hashes, const MatchOptions& options) {
    MatchResult result; //For storing candidate pairs
    const std::size_t n = hashes.size(); //Num of images
    const bool capped = options.max_candidates_per_image > 0; //Is capping enabled?

    std :: vector<BoundedHeap> best(capped ? n : 0); //One heap per image. BAD CHOICE. IMPROVEMENT
    for (std :: size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            ++result.comparisons;
            CandidatePair p;
            p.a = i;
            p.b = j;
            p.d_ahash = hamming_distance(hashes[i].ahash, hashes[j].ahash);
            p.d_dhash = hamming_distance(hashes[i].dhash, hashes[j].dhash);
            p.d_phash = hamming_distance(hashes[i].phash, hashes[j].phash);
            p.d_dhash_oriented = dhash_distance_any_orientation(hashes[i], hashes[j]);
            //candidate generation uses the best orientation. The feature
            //stays the plain distance, because the model was trained on it.
            // const int d_oriented = dhash_distance_any_orientation(hashes[i], hashes[j]);

            const bool qualifies = p.d_ahash <= options.ahash_radius ||
                                   p.d_dhash <= options.dhash_radius ||
                                   p.d_phash <= options.phash_radius ||
                                   p.d_dhash_oriented <= options.dhash_radius;
            if (!qualifies) continue; //nothing is in bounds
            ++result.pairs_before_cap;
            if (!capped) {
                result.pairs.push_back(p);
                continue;
            }

            for (std::size_t endpoint : {i, j}) { //Both heaps for image i and j
                BoundedHeap& h = best[endpoint];
                if (h.size() < options.max_candidates_per_image) {
                    h.push(p);
                }
                else if (p.best_distance() < h.top().best_distance()) { //Max heap at work
                    h.pop();
                    h.push(p);
                }
            }
        }
    }
    if (!capped) return result;
    // Drain the heaps and dedupe as a pair kept by both endpoints appears twice.
    std::vector<CandidatePair> kept;
    for (std::size_t i = 0; i < n; ++i) {
        if (best[i].size() >= options.max_candidates_per_image) {
            ++result.images_capped;
        }
        while (!best[i].empty()) {
            kept.push_back(best[i].top());
            best[i].pop();
        }
    }
    std::sort(kept.begin(), kept.end(),
          [](const CandidatePair& x, const CandidatePair& y) {
              return key_of(x) < key_of(y);
          });
    kept.erase(std::unique(kept.begin(), kept.end(),
                           [](const CandidatePair& x, const CandidatePair& y) {
                               return key_of(x) == key_of(y);
                           }),
               kept.end());
    result.pairs = std::move(kept);
    return result;
}
}
