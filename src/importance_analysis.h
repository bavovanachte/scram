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

/// @file importance_analysis.h
/// Contains functionality to do numerical analysis
/// of importance factors.

#ifndef SCRAM_SRC_IMPORTANCE_ANALYSIS_H_
#define SCRAM_SRC_IMPORTANCE_ANALYSIS_H_

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "bdd.h"
#include "event.h"
#include "probability_analysis.h"
#include "settings.h"

namespace scram {

/// @struct ImportanceFactors
/// Collection of importance factors for variables.
struct ImportanceFactors {
  double mif;  ///< Birnbaum marginal importance factor.
  double cif;  ///< Critical importance factor.
  double dif;  ///< Fussel-Vesely diagnosis importance factor.
  double raw;  ///< Risk achievement worth factor.
  double rrw;  ///< Risk reduction worth factor.
};

/// @class ImportanceAnalysis
/// Analysis of importance factors of risk model variables.
class ImportanceAnalysis : public Analysis {
 public:
  using BasicEventPtr = std::shared_ptr<BasicEvent>;

  /// Importance analysis
  /// on the fault tree represented by
  /// its probability analysis.
  ///
  /// @param[in] prob_analysis  Completed probability analysis.
  explicit ImportanceAnalysis(const ProbabilityAnalysis* prob_analysis);

  virtual ~ImportanceAnalysis() = default;

  /// Performs quantitative analysis of importance factors
  /// of basic events in minimal cut sets.
  ///
  /// @pre Analysis is called only once.
  void Analyze() noexcept;

  /// @returns Map with basic events and their importance factors.
  ///
  /// @pre The importance analysis is done.
  const std::unordered_map<std::string, ImportanceFactors>& importance() const {
    return importance_;
  }

  /// @returns A collection of important events and their importance factors.
  ///
  /// @pre The importance analysis is done.
  const std::vector<std::pair<BasicEventPtr, ImportanceFactors>>&
  important_events() const {
    return important_events_;
  }

 protected:
  /// Gathers all events present in cut sets.
  /// Only this events can have importance factors.
  ///
  /// @tparam CutSet  An iterable container of unique elements.
  ///
  /// @param[in] graph  Boolean graph with basic event indices and pointers.
  /// @param[in] cut_sets  Cut sets with basic event indices.
  ///
  /// @returns A unique collection of importance basic events.
  template<typename CutSet>
  std::vector<std::pair<int, BasicEventPtr>> GatherImportantEvents(
      const BooleanGraph* graph,
      const std::vector<CutSet>& cut_sets) noexcept;

 private:
  /// Find all events that are in the cut sets.
  ///
  /// @returns Indices and pointers to the basic events.
  virtual std::vector<std::pair<int, BasicEventPtr>>
  GatherImportantEvents() noexcept = 0;

  /// Calculates Marginal Importance Factor.
  ///
  /// @param[in] index  Positive index of an event.
  ///
  /// @returns Calculated value for MIF.
  virtual double CalculateMif(int index) noexcept = 0;

  /// @returns Total probability from the probability analysis.
  virtual double p_total() noexcept = 0;

  /// Container for basic event importance factors.
  std::unordered_map<std::string, ImportanceFactors> importance_;
  /// Container of pointers to important events and their importance factors.
  std::vector<std::pair<BasicEventPtr, ImportanceFactors>> important_events_;
};

template<typename CutSet>
std::vector<std::pair<int, std::shared_ptr<BasicEvent>>>
ImportanceAnalysis::GatherImportantEvents(
    const BooleanGraph* graph,
    const std::vector<CutSet>& cut_sets) noexcept {
  std::vector<std::pair<int, BasicEventPtr>> important_events;
  std::unordered_set<int> unique_indices;
  for (const auto& cut_set : cut_sets) {
    for (int index : cut_set) {
      if (unique_indices.count(std::abs(index))) continue;  // Most likely.
      int pos_index = std::abs(index);
      unique_indices.insert(pos_index);
      important_events.emplace_back(pos_index,
                                    graph->GetBasicEvent(pos_index));
    }
  }
  return important_events;
}

/// @class ImportanceAnalyzer
/// Analyzer of importance factors
/// with the help from probability analyzers.
///
/// @tparam Algorithm  Qualitative analysis algorithm.
template<typename Algorithm, typename Calculator>
class ImportanceAnalyzer : public ImportanceAnalysis {
 public:
  /// Constructs importance analyzer from probability analyzer.
  /// Probability analyzer facilities are used
  /// to calculate the total and conditional probabilities for factors.
  ///
  /// @param[in] prob_analyzer  Instantiated probability analyzer.
  ///
  /// @pre Probability analyzer can work with modified probability values.
  ///
  /// @post Probability analyzer's probability values are
  ///       reset to the original values (event probabilities).
  explicit ImportanceAnalyzer(
      ProbabilityAnalyzer<Algorithm, Calculator>* prob_analyzer)
      : ImportanceAnalysis::ImportanceAnalysis(prob_analyzer),
        prob_analyzer_(prob_analyzer) {}

  std::vector<std::pair<int, BasicEventPtr>> GatherImportantEvents() noexcept {
    return ImportanceAnalysis::GatherImportantEvents(
        prob_analyzer_->graph(),
        prob_analyzer_->fta()->algorithm()->GetGeneratedMcs());
  }

  double CalculateMif(int index) noexcept;

  double p_total() noexcept { return prob_analyzer_->p_total(); }

 private:
  /// Calculator of the total probability.
  ProbabilityAnalyzer<Algorithm, Calculator>* prob_analyzer_;
};

template<typename Algorithm, typename Calculator>
double
ImportanceAnalyzer<Algorithm, Calculator>::CalculateMif(int index) noexcept {
  std::vector<double>& var_probs = prob_analyzer_->var_probs();
  // Calculate P(top/event)
  var_probs[index] = 1;
  double p_e = prob_analyzer_->CalculateTotalProbability();
  assert(p_e >= 0);
  if (p_e > 1) p_e = 1;

  // Calculate P(top/Not event)
  var_probs[index] = 0;
  double p_not_e = prob_analyzer_->CalculateTotalProbability();
  assert(p_not_e >= 0);
  if (p_not_e > 1) p_not_e = 1;

  // Restore the probability.
  var_probs[index] = prob_analyzer_->graph()->GetBasicEvent(index)->p();
  return p_e - p_not_e;
}

/// @class ImportanceAnalyzer<Algorithm, Bdd>
/// Specialization of importance analyzer with Binary Decision Diagrams.
///
/// @tparam Algorithm  Qualitative analysis algorithm.
template<typename Algorithm>
class ImportanceAnalyzer<Algorithm, Bdd> : public ImportanceAnalysis {
 public:
  /// Constructs importance analyzer from probability analyzer.
  /// Probability analyzer facilities are used
  /// to calculate the total and conditional probabilities for factors.
  ///
  /// @param[in] prob_analyzer  Instantiated probability analyzer.
  explicit ImportanceAnalyzer(
      ProbabilityAnalyzer<Algorithm, Bdd>* prob_analyzer)
      : ImportanceAnalysis::ImportanceAnalysis(prob_analyzer),
        prob_analyzer_(prob_analyzer),
        bdd_graph_(prob_analyzer->bdd_graph()) {}

  std::vector<std::pair<int, BasicEventPtr>> GatherImportantEvents() noexcept {
    return ImportanceAnalysis::GatherImportantEvents(
        prob_analyzer_->graph(),
        prob_analyzer_->fta()->algorithm()->GetGeneratedMcs());
  }

  double CalculateMif(int index) noexcept;

  double p_total() noexcept { return prob_analyzer_->p_total(); }

 private:
  using VertexPtr = std::shared_ptr<Vertex>;
  using ItePtr = std::shared_ptr<Ite>;

  /// Calculates Marginal Importance Factor of a variable.
  ///
  /// @param[in] vertex  The root vertex of a function graph.
  /// @param[in] order  The identifying order of the variable.
  /// @param[in] mark  A flag to mark traversed vertices.
  ///
  /// @returns Importance factor value.
  ///
  /// @note Probability factor fields are used to save results.
  /// @note The graph needs cleaning its marks after this function
  ///       because the graph gets continuously-but-partially marked.
  double CalculateMif(const VertexPtr& vertex, int order, bool mark) noexcept;

  /// Retrieves memorized probability values for BDD function graphs.
  ///
  /// @param[in] vertex  Vertex with calculated probabilities.
  ///
  /// @returns Saved probability value of the vertex.
  double RetrieveProbability(const VertexPtr& vertex) noexcept;

  /// Calculator of the total probability.
  ProbabilityAnalyzer<Algorithm, Bdd>* prob_analyzer_;

  Bdd* bdd_graph_;  ///< Binary decision diagram for the analyzer.
};

template<typename Algorithm>
double
ImportanceAnalyzer<Algorithm, Bdd>::CalculateMif(int index) noexcept {
  VertexPtr root = bdd_graph_->root().vertex;
  if (root->terminal()) return 0;
  bool original_mark = Ite::Ptr(root)->mark();

  int order = bdd_graph_->index_to_order().find(index)->second;
  double mif = ImportanceAnalyzer::CalculateMif(bdd_graph_->root().vertex,
                                                order,
                                                !original_mark);
  bdd_graph_->ClearMarks(original_mark);
  return mif;
}

template<typename Algorithm>
double ImportanceAnalyzer<Algorithm, Bdd>::CalculateMif(const VertexPtr& vertex,
                                                        int order,
                                                        bool mark) noexcept {
  if (vertex->terminal()) return 0;
  ItePtr ite = Ite::Ptr(vertex);
  if (ite->mark() == mark) return ite->factor();
  ite->mark(mark);
  if (ite->order() > order) {
    if (!ite->module()) {
      ite->factor(0);
    } else {  /// @todo Detect if the variable is in the module.
      // The assumption is
      // that the order of a module is always larger
      // than the order of its variables.
      double high = ImportanceAnalyzer::RetrieveProbability(ite->high());
      double low = ImportanceAnalyzer::RetrieveProbability(ite->low());
      if (ite->complement_edge()) low = 1 - low;
      const Bdd::Function& res = bdd_graph_->gates().find(ite->index())->second;
      double mif = ImportanceAnalyzer::CalculateMif(res.vertex, order, mark);
      if (res.complement) mif = -mif;
      ite->factor((high - low) * mif);
    }
  } else if (ite->order() == order) {
    assert(!ite->module() && "A variable can't be a module.");
    double high = ImportanceAnalyzer::RetrieveProbability(ite->high());
    double low = ImportanceAnalyzer::RetrieveProbability(ite->low());
    if (ite->complement_edge()) low = 1 - low;
    ite->factor(high - low);
  } else  {
    assert(ite->order() < order);
    double var_prob = 0;
    if (ite->module()) {
      const Bdd::Function& res = bdd_graph_->gates().find(ite->index())->second;
      var_prob = ImportanceAnalyzer::RetrieveProbability(res.vertex);
      if (res.complement) var_prob = 1 - var_prob;
    } else {
      var_prob = prob_analyzer_->var_probs()[ite->index()];
    }
    double high = ImportanceAnalyzer::CalculateMif(ite->high(), order, mark);
    double low = ImportanceAnalyzer::CalculateMif(ite->low(), order, mark);
    if (ite->complement_edge()) low = -low;
    ite->factor(var_prob * high + (1 - var_prob) * low);
  }
  return ite->factor();
}

template<typename Algorithm>
double ImportanceAnalyzer<Algorithm, Bdd>::RetrieveProbability(
    const VertexPtr& vertex) noexcept {
  if (vertex->terminal()) return 1;
  return Ite::Ptr(vertex)->prob();
}

}  // namespace scram

#endif  // SCRAM_SRC_IMPORTANCE_ANALYSIS_H_
