//
// Created by shrey on 30-Jul-26.
//


#include "dejaview/model.hpp"

#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace dejaview {

float PairClassifier::probability(const PairFeatures& f) const {
    if (!loaded) return 0.0f;
    const auto x = f.to_array();

    double z = intercept;
    for (std::size_t i = 0; i < PairFeatures::kCount; ++i) {
        const double s = (scale[i] != 0.0) ? scale[i] : 1.0;
        z += coef[i] * ((static_cast<double>(x[i]) - mean[i]) / s);
    }
    return static_cast<float>(1.0 / (1.0 + std::exp(-z)));
}

bool load_classifier(const fs::path& p, PairClassifier& out,
                     std::string& error) {
    std::ifstream in(p);
    if (!in) {
        error = "cannot open model file: " + p.string();
        return false;
    }

    json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        error = std::string("malformed JSON: ") + e.what();
        return false;
    }

    try {
        if (j.value("schema_version", 0) != 1) {
            error = "unsupported model schema_version";
            return false;
        }
        if (j.value("model", std::string()) != "logistic_regression") {
            error = "unsupported model type: " + j.value("model", std::string());
            return false;
        }

        // --- Schema check: names must match ours exactly, in order ---------
        const auto names = j.at("feature_names").get<std::vector<std::string>>();
        const auto expected = PairFeatures::names();
        if (names.size() != PairFeatures::kCount) {
            error = "model expects " + std::to_string(names.size()) +
                    " features, this build has " +
                    std::to_string(PairFeatures::kCount);
            return false;
        }
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (names[i] != expected[i]) {
                error = "feature mismatch at index " + std::to_string(i) +
                        ": model has '" + names[i] + "', build has '" +
                        expected[i] + "'";
                return false;
            }
        }

        out.mean = j.at("mean").get<std::vector<double>>();
        out.scale = j.at("scale").get<std::vector<double>>();
        out.coef = j.at("coef").get<std::vector<double>>();
        out.intercept = j.at("intercept").get<double>();
        out.threshold = j.value("threshold", 0.5);

        if (out.mean.size() != PairFeatures::kCount ||
            out.scale.size() != PairFeatures::kCount ||
            out.coef.size() != PairFeatures::kCount) {
            error = "weight vector length does not match feature count";
            return false;
        }
    } catch (const std::exception& e) {
        error = std::string("missing or invalid field: ") + e.what();
        return false;
    }

    out.loaded = true;
    return true;
}

} 