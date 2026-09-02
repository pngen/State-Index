// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef STATEINDEX_ENGINE_HPP
#define STATEINDEX_ENGINE_HPP

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "stateindex/strong.hpp"
#include "stateindex/enums.hpp"
#include "stateindex/record.hpp"
#include "stateindex/location.hpp"
#include "stateindex/invalidation.hpp"
#include "stateindex/query.hpp"
#include "stateindex/result.hpp"
#include "stateindex/index.hpp"
#include "stateindex/authority.hpp"
#include "stateindex/accounting.hpp"

namespace stateindex {

// A single candidate after hard-filter elimination and before ranking.
struct CandidateAssessment {
    StateRecordId record_id{};
    StateGeneration generation{};
    bool in_scope = false;
    RejectionReason rejection = RejectionReason::NONE;
    std::string rejection_note;
    // Named ranking factors (higher = better), aligned with kFactorOrder.
    std::vector<double> factors;
    std::vector<std::string> factor_notes;
    std::optional<PlacementId> best_placement;
};

// The central runtime. It owns the authoritative record set, the secondary
// indexes, the authority registry, invalidations, tombstones, accounting, and
// the query planner. It is safe for a single coordinator process and is used by
// the multiprocess proof through the framed protocol server.
class StateIndexEngine {
public:
    explicit StateIndexEngine(CoordinatorEpoch epoch = CoordinatorEpoch(1));

    // ---- Authority ----
    AuthorityRegistry& authority() noexcept { return authority_; }
    void register_worker(WorkerId w, WorkerBootId b);
    void unregister_worker(WorkerBootId b);
    void advance_epoch();
    CoordinatorEpoch epoch() const noexcept { return authority_.epoch(); }

    // ---- Mutations (all carry a MutationEnvelope) ----
    MutationVerdictDetail register_state(StateRecord rec, const MutationEnvelope& env);
    MutationVerdictDetail add_location(const StateId& state, StateLocation loc, const MutationEnvelope& env);
    MutationVerdictDetail remove_location(const StateId& state, const PlacementId& placement, const MutationEnvelope& env);
    MutationVerdictDetail invalidate(const InvalidationRecord& inv, const MutationEnvelope& env);
    MutationVerdictDetail tombstone(const TombstoneRecord& t, const MutationEnvelope& env);

    // ---- Queries ----
    QueryResult query(const QueryDescriptor& q);
    QueryResult query_historical(const QueryDescriptor& q);

    // ---- Inspection ----
    std::optional<StateRecord> get_record(StateRecordId id) const;
    std::vector<StateRecord> canonical_records() const;
    const std::unordered_map<StateId, StateRecordId>& current() const noexcept { return current_; }
    std::vector<StateRecord> history_of(const StateId& state) const;
    std::vector<StateLocation> locations_of(const StateId& state) const;
    std::vector<std::string> dependencies_of(const StateId& state) const;
    const SecondaryIndexes& indexes() const noexcept { return indexes_; }

    // ---- Consistency ----
    std::vector<std::string> invariant_check() const;
    std::vector<std::string> index_verification() const;

    // ---- Persistence ----
    void save(const std::string& path) const;
    void load(const std::string& path);

    // ---- Accounting ----
    Accounting& accounting() noexcept { return accounting_; }
    const Accounting& accounting() const noexcept { return accounting_; }

    // ---- Explanation ----
    std::string explain_query(const QueryDescriptor& q, const QueryResult& r) const;
    std::string explain_candidate(const CandidateAssessment& c) const;
    std::string explain_rejection(RejectionReason r, const std::string& note) const;
    std::string explain_invalidation(const InvalidationRecord& inv) const;
    std::string explain_tombstone(const TombstoneRecord& t) const;
    std::string explain_supersession(const StateRecord& current, const StateRecord& previous) const;
    std::string explain_recovery(const std::string& note) const;
    std::string explain_location(const StateLocation& loc) const;

    // ---- Policy ----
    struct Policy {
        bool allow_historical_current_visibility = false;
        bool demand_usable_location_for_available = true;
    } policy;

    // Planner constants: priority order of named ranking factors (highest
    // priority first). Exposed so the ordering is transparent, not hidden.
    static const std::vector<RankingFactor>& factor_order();

private:
    AuthorityRegistry authority_;
    SecondaryIndexes indexes_;
    Accounting accounting_;
    std::unordered_map<StateRecordId, StateRecord> records_;
    std::unordered_map<StateId, StateRecordId> current_;
    std::unordered_map<StateId, std::vector<StateRecordId>> history_;
    std::vector<InvalidationRecord> invalidations_;
    std::unordered_map<StateId, TombstoneRecord> tombstones_;
    std::uint64_t invalidation_seq_ = 0;
    bool historical_mode_ = false;
    // One narrow lock at the public-method boundary. Mutations and queries are
    // quick; no blocking socket or callback work is performed while it is held.
    // Read-only inspection helpers are const and lock-free; they are not called
    // concurrently in the multiprocess proof.
    mutable std::mutex mtx_;

    std::optional<StateRecord> find_state(const StateId& state) const;
    bool is_tombstoned(const StateId& state, const StateGeneration& gen) const;
    bool dep_satisfied(const StateRecord& rec) const;
    void set_current(const StateRecord& rec);
    void recompute_current();
    CandidateAssessment evaluate_candidate(const StateRecord& rec, const QueryDescriptor& q) const;
    std::vector<StateRecordId> initial_candidate_ids(const QueryDescriptor& q) const;
    QueryResult run_query(const QueryDescriptor& q, bool historical);
    void rebuild_after_load();
    void verify_semantic_digest(const StateRecord& rec) const;
};

}  // namespace stateindex

#endif  // STATEINDEX_ENGINE_HPP
