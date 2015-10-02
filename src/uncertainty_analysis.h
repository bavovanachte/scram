/*
 * Copyright (C) 2014-2015 Olzhas Rakhimov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/// @file uncertainty_analysis.h
/// Provides functionality for uncertainty analysis
/// with Monte Carlo method.

#ifndef SCRAM_SRC_UNCERTAINTY_ANALYSIS_H_
#define SCRAM_SRC_UNCERTAINTY_ANALYSIS_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "analysis.h"
#include "event.h"
#include "probability_analysis.h"
#include "settings.h"

namespace scram {

/// @class UncertaintyAnalysis
/// Uncertainty analysis and statistics
/// for top event or gate probabilities
/// from minimal cut sets
/// and probability distributions of basic events.
class UncertaintyAnalysis : public Analysis {
 public:
  using BasicEventPtr = std::shared_ptr<BasicEvent>;

  /// Uncertainty analysis
  /// on the fault tree processed
  /// by probability analysis.
  ///
  /// @param[in] prob_analysis  Completed probability analysis.
  explicit UncertaintyAnalysis(const ProbabilityAnalysis* prob_analysis);

  virtual ~UncertaintyAnalysis() = default;

  /// Performs quantitative analysis on the total probability.
  ///
  /// @note  Undefined behavior if analysis called two or more times.
  void Analyze() noexcept;

  /// @returns Mean of the final distribution.
  double mean() const { return mean_; }

  /// @returns Standard deviation of the final distribution.
  double sigma() const { return sigma_; }

  /// @returns Error factor for 95% confidence level.
  double error_factor() const { return error_factor_; }

  /// @returns 95% confidence interval of the mean.
  const std::pair<double, double>& confidence_interval() const {
    return confidence_interval_;
  }

  /// @returns The distribution histogram.
  const std::vector<std::pair<double, double> >& distribution() const {
    return distribution_;
  }

  /// @returns Quantiles of the distribution.
  const std::vector<double>& quantiles() const { return quantiles_; }

 protected:
  /// Performs Monte Carlo Simulation
  /// by sampling the probability distributions
  /// and providing the final sampled values of the final probability.
  ///
  /// @returns Sampled values.
  virtual std::vector<double> Sample() noexcept = 0;

  /// Gathers basic events that have distributions.
  ///
  /// @param[in] graph  Boolean graph with the variables.
  ///
  /// @returns The gathered uncertain basic events.
  std::vector<std::pair<int, BasicEvent*>> FilterUncertainEvents(
      const BooleanGraph* graph) noexcept;

 private:
  /// Calculates statistical values from the final distribution.
  ///
  /// @param[in] samples  Gathered samples for statistical analysis.
  void CalculateStatistics(const std::vector<double>& samples) noexcept;

  double mean_;  ///< The mean of the final distribution.
  double sigma_;  ///< The standard deviation of the final distribution.
  double error_factor_;  ///< Error factor for 95% confidence level.
  /// The confidence interval of the distribution.
  std::pair<double, double> confidence_interval_;
  /// The histogram density of the distribution with lower bounds and values.
  std::vector<std::pair<double, double> > distribution_;
  /// The quantiles of the distribution.
  std::vector<double> quantiles_;
};

/// @class UncertaintyAnalyzer
/// Uncertainty analysis facility.
///
/// @tparam Algorithm  Qualitative analysis algorithm.
/// @tparam Calculator  Quantitative analysis calculator.
template<typename Algorithm, typename Calculator>
class UncertaintyAnalyzer : public UncertaintyAnalysis {
 public:
  /// Constructs uncertainty analyzer from probability analyzer.
  /// Probability analyzer facilities are used
  /// to calculate the total probability for sampling.
  ///
  /// @param[in] prob_analyzer  Instantiated probability analyzer.
  ///
  /// @pre Probability analyzer can work with modified probability values.
  ///
  /// @post Probability analyzer's probability values are
  ///       reset to the original values (event probabilities).
  explicit UncertaintyAnalyzer(
      ProbabilityAnalyzer<Algorithm, Calculator>* prob_analyzer)
      : UncertaintyAnalysis::UncertaintyAnalysis(prob_analyzer),
        prob_analyzer_(prob_analyzer) {}

  /// @returns Samples of the total probability.
  std::vector<double> Sample() noexcept override;

 private:
  /// Calculator of the total probability.
  ProbabilityAnalyzer<Algorithm, Calculator>* prob_analyzer_;
};

template<typename Algorithm, typename Calculator>
std::vector<double>
UncertaintyAnalyzer<Algorithm, Calculator>::Sample() noexcept {
  std::vector<std::pair<int, BasicEvent*>> uncertain_events =
      UncertaintyAnalysis::FilterUncertainEvents(prob_analyzer_->graph());
  std::vector<double>& var_probs = prob_analyzer_->var_probs();
  std::vector<double> samples;
  samples.reserve(kSettings_.num_trials());
  for (int i = 0; i < kSettings_.num_trials(); ++i) {
    // Reset distributions.
    for (const auto& event : uncertain_events) event.second->Reset();

    // Sample all basic events with distributions.
    for (const auto& event : uncertain_events) {
      double prob = event.second->SampleProbability();
      if (prob < 0) {  // Adjust if out of range.
        prob = 0;
      } else if (prob > 1) {
        prob = 1;
      }
      var_probs[event.first] = prob;
    }
    double result = prob_analyzer_->CalculateTotalProbability();
    assert(result >= 0);
    if (result > 1) result = 1;
    samples.push_back(result);
  }
  // Cleanup.
  for (const auto& event : uncertain_events)
    var_probs[event.first] = event.second->p();

  return samples;
}

}  // namespace scram

#endif  // SCRAM_SRC_UNCERTAINTY_ANALYSIS_H_
