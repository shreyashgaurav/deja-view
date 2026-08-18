//
// Created by shrey on 31-Jul-26.
//

#include "dejaview/cluster.hpp"

#include <algorithm>
#include <numeric>

namespace dejaview {
namespace {
// By claude Code (verified )
// Union-find (disjoint set) with path compression and union by size. Near-constant time per operation;
// the classic tool for "group things connected by these relationships".
class UnionFind {
public:
    explicit UnionFind(std::size_t n) : parent_(n), size_(n, 1) {
        std::iota(parent_.begin(), parent_.end(), 0);
    }

    std::size_t find(std::size_t x) {
        while (parent_[x] != x) {
            parent_[x] = parent_[parent_[x]];  // path halving
            x = parent_[x];
        }
        return x;
    }

    std::size_t set_size(std::size_t x) { return size_[find(x)]; }

    void unite(std::size_t a, std::size_t b) {
        a = find(a);
        b = find(b);
        if (a == b) return;
        if (size_[a] < size_[b]) std::swap(a, b);
        parent_[b] = a;
        size_[a] += size_[b];
    }

private:
    std::vector<std::size_t> parent_;
    std::vector<std::size_t> size_;
};

}

ClusterResult cluster_pairs(const std::vector<ScoredPair>& pairs,
                            std::size_t item_count,
                            const ClusterOptions& options) {
    ClusterResult result;
    UnionFind uf(item_count);


    //Plain union-find would ruin the output (It will give a bunch of images where individual images ar similar but as a group its is non-sense)
    //Ex: A <-> B : 60% (OK). B <-> C : 60% (OK). Plain union-find => {A, B, C} even though A and C might not be simlar
    //We are making use of three mech. to handle this:
    //1. Process confident pairs first => Strong pairs form groups first
    //2. Weak pairs can join, but never bridge.
    //3. A hard size cap. (56 images)
    //Strongest evidence first: a confident pair should establish the group before weaker pairs get a chance to attach to it.
    std::vector<ScoredPair> sorted = pairs;
    std::sort(sorted.begin(), sorted.end(),
              [](const ScoredPair& x, const ScoredPair& y) {
                  return x.probability > y.probability;
              });

    // Track the weakest edge accepted into each set, so the report can show how cohesive a group is (a group held together by a 0.52 edge deserve more scepticism than one where every edge is 0.99).
    std::vector<float> weakest(item_count, 1.0f);

    for (const ScoredPair& p : sorted) {
        const std::size_t ra = uf.find(p.a);
        const std::size_t rb = uf.find(p.b);
        if (ra == rb) continue;  // already together

        if (options.max_cluster_size > 0 &&
            uf.set_size(ra) + uf.set_size(rb) > options.max_cluster_size) {
            ++result.size_merges_blocked;
            continue;
        }

        // Weak edges may only attach a singleton, never bridge two groups.
        if (p.probability < options.core_probability) {
            const bool a_alone = uf.set_size(ra) == 1;
            const bool b_alone = uf.set_size(rb) == 1;
            if (!a_alone && !b_alone) {
                ++result.weak_merges_blocked;
                continue;
            }
        }

        const float w = std::min({weakest[ra], weakest[rb], p.probability});
        uf.unite(ra, rb);
        weakest[uf.find(ra)] = w;
    }

    // Collect members by root.
    std::vector<std::vector<std::size_t>> by_root(item_count);
    for (std::size_t i = 0; i < item_count; ++i) {
        by_root[uf.find(i)].push_back(i);
    }
    for (std::size_t root = 0; root < item_count; ++root) {
        if (by_root[root].size() < 2) continue;  // singletons aren't groups
        Cluster c;
        c.members = std::move(by_root[root]);
        c.weakest_link = weakest[root];
        result.clusters.push_back(std::move(c));
    }

    // Largest groups first - most reclaimable space, most user interest.
    std::sort(result.clusters.begin(), result.clusters.end(),
              [](const Cluster& x, const Cluster& y) {
                  return x.members.size() > y.members.size();
              });
    return result;
}

std::size_t recommend_keep(const std::vector<std::size_t>& members,
                           const std::vector<ImageFeatures>& features) {
    std::size_t best = members.front();
    for (std::size_t idx : members) {
        const auto& cand = features[idx];
        const auto& cur = features[best];
        const long long cand_px = 1LL * cand.width * cand.height;
        const long long cur_px = 1LL * cur.width * cur.height;
        if (cand_px > cur_px ||
            (cand_px == cur_px && cand.file_size > cur.file_size)) {
            best = idx;
        }
    }
    return best;
}

}