//
// Created by shrey on 18-Aug-26.
//



#include "dejaview/report.hpp"
#include <fstream>
#include <nlohmann/json.hpp> //To work with Json

#include <algorithm>

namespace fs = std::filesystem;
using json = nlohmann::json; //ALiasing


namespace dejaview {

bool write_report(const fs::path& out, const ReportInput& in,
                  std::string& error) {
    if (!in.files || !in.hashed_index || !in.features || !in.clusters) {
        error = "report input is missing required pipeline results";
        return false;
    }

    json j;
    j["schema_version"] = 1;   //consumers check this before parsing

    //scan_info
    j["scan_info"] = {
        {"roots", in.roots},
        {"entries_visited", in.entries_visited},
        {"images_found", in.images_found},
        {"skipped_non_image", in.skipped_non_image},
        {"total_bytes", in.total_bytes},
        {"timings_ms", {{"scan", in.scan_ms},
                        {"hash_and_features", in.hash_ms},
                        {"match", in.match_ms}}},
        {"candidate_pairs", in.candidates},
        {"classifier", {{"loaded", in.model_loaded},
                        {"threshold", in.threshold}}},
    };

    //errors
    j["errors"] = json::array();
    for (const auto& e : in.errors) {
        j["errors"].push_back({{"path", e.path.string()}, {"message", e.message}});
    }

    // Helper: describe one image by its index into the hashed set.
    auto image_json = [&](std::size_t hashed_i) {
        const auto& fe = (*in.files)[(*in.hashed_index)[hashed_i]];
        const auto& ft = (*in.features)[hashed_i];
        return json{{"path", fe.path.string()},
                    {"bytes", fe.size_bytes},
                    {"width", ft.width},
                    {"height", ft.height},
                    {"format", fe.format == ImageFormat::Jpeg ? "jpeg" : "png"}};
    };

    //groups
    //// Exact-duplicate groups first: they are certainties, not predictions.
    j["groups"] = json::array();
    std::uintmax_t reclaimable_total = 0;
    int group_id = 0;

    if (in.exact) {
        for (const auto& g : in.exact->groups) {
            json members = json::array();
            std::uintmax_t size = 0;
            for (std::size_t fi : g) {
                const auto& fe = (*in.files)[fi];
                members.push_back({{"path", fe.path.string()},
                                   {"bytes", fe.size_bytes}});
                size = fe.size_bytes;
            }
            const std::uintmax_t reclaim = size * (g.size() - 1);
            reclaimable_total += reclaim;
            j["groups"].push_back({
                {"id", group_id++},
                {"type", "exact"},
                {"members", members},
                {"keep", (*in.files)[g.front()].path.string()},
                {"keep_rule", "byte-identical: any copy will do"},
                {"reclaimable_bytes", reclaim},
            });
        }
    }

    //Near-duplicate groups, with the evidence behind each pair.
    // for (const auto& c in_clusters : {0}) { (void)in_clusters; break; } // placeholder
    for (const auto& c : in.clusters->clusters) {
        const std::size_t keep = recommend_keep(c.members, *in.features);

        json members = json::array();
        std::uintmax_t reclaim = 0;
        for (std::size_t m : c.members) {
            json mj = image_json(m);
            mj["keep"] = (m == keep);
            members.push_back(mj);
            if (m != keep) reclaim += (*in.features)[m].file_size;
        }
        reclaimable_total += reclaim;

        //Pair-level evidence: every scored pair whose endpoints are both in
        // this group. This is what makes the report auditable rather than a
        // bare verdict.
        json pairs = json::array();
        if (in.scored_pairs) {
            for (std::size_t k = 0; k < in.scored_pairs->size(); ++k) {
                const auto& sp = (*in.scored_pairs)[k];
                const bool a_in = std::find(c.members.begin(), c.members.end(),
                                            sp.a) != c.members.end();
                const bool b_in = std::find(c.members.begin(), c.members.end(),
                                            sp.b) != c.members.end();
                if (!a_in || !b_in) continue;

                json pj = {{"a", sp.a}, {"b", sp.b},
                           {"probability", sp.probability}};
                if (in.pair_features && k < in.pair_features->size()) {
                    json feats = json::object();
                    const auto names = PairFeatures::names();
                    const auto vals = (*in.pair_features)[k].to_array();
                    for (std::size_t f = 0; f < names.size(); ++f) {
                        feats[std::string(names[f])] = vals[f];
                    }
                    pj["features"] = feats;
                }
                pairs.push_back(pj);
            }
        }

        j["groups"].push_back({
            {"id", group_id++},
            {"type", "near"},
            {"members", members},
            {"weakest_link", c.weakest_link},
            {"keep", (*in.files)[(*in.hashed_index)[keep]].path.string()},
            {"keep_rule", "highest resolution, then largest file"},
            {"reclaimable_bytes", reclaim},
            {"pairs", pairs},
        });
    }

    j["summary"] = {
        {"total_groups", j["groups"].size()},
        {"exact_groups", in.exact ? in.exact->groups.size() : 0},
        {"near_groups", in.clusters->clusters.size()},
        {"reclaimable_bytes", reclaimable_total},
        {"weak_merges_blocked", in.clusters->weak_merges_blocked},
    };

    std::ofstream f(out);
    if (!f) {
        error = "cannot write report: " + out.string();
        return false;
    }
    f << j.dump(2) << '\n';
    if (!f) {
        error = "write failed";
        return false;
    }
    return true;
}

}