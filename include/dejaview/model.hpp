//
// Created by shrey on 30-Jul-26.
//

#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "dejaview/features.hpp"

namespace dejaview {

    // Logistic regression trained offline by training/train.py.
    // Inference is: sigmoid(intercept + sum_i coef_i * (x_i - mean_i) / scale_i)
    struct PairClassifier {
        std::vector<double> mean, scale, coef;
        double intercept = 0.0;
        double threshold = 0.5;
        bool loaded = false;

        // Calibrated probability that the pair is a duplicate, in [0,1].
        float probability(const PairFeatures& f) const;
        bool is_duplicate(const PairFeatures& f) const {
            return probability(f) >= threshold;
        }
    };

    // Loads and VALIDATES the weights file. The feature names in the artifact must
    // match PairFeatures::names() exactly, in order - otherwise a model trained
    // against an older feature set would silently apply each weight to the wrong
    // column. Refusing to load is the whole point (SRS ML-I1).
    bool load_classifier(const std::filesystem::path& p, PairClassifier& out,
                         std::string& error);

}  // namespace dejaview
