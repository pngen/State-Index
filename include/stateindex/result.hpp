// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef STATEINDEX_RESULT_HPP
#define STATEINDEX_RESULT_HPP

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "stateindex/strong.hpp"
#include "stateindex/enums.hpp"
#include "stateindex/cost.hpp"
#include "stateindex/bytebuf.hpp"
#include "stateindex/limits.hpp"

namespace stateindex {

// A ranked candidate exposes every named factor that contributed to its
// ordering. There is no opaque master score: the rank is the deterministic
// ordering of these explicit factors, and the reason string explains it.
struct CandidateRanking {
    StateRecordId record_id{};
    StateGeneration generation{};
    std::optional<PlacementId> best_placement;
    std::vector<std::pair<RankingFactor, double>> factors;  // higher = better
    std::vector<std::string> factor_notes;
    std::string summary;
};

struct EliminatedCandidate {
    StateRecordId record_id{};
    RejectionReason reason = RejectionReason::NONE;
    std::string note;
};

// QueryResult carries the outcome plus the evidence that justified it. For
// FOUND_MULTIPLE it returns a ranked candidate list rather than silently
// choosing one.
struct QueryResult {
    ResultId result_id{};
    QueryId query_id{};
    QueryPlanId query_plan_id{};
    QueryGeneration query_generation{};
    QueryOutcome outcome = QueryOutcome::UNKNOWN;

    std::optional<StateRecordId> selected_record_id;
    std::optional<StateGeneration> selected_generation;
    std::optional<PlacementId> selected_placement;
    std::optional<StateId> selected_state_id;

    std::string compatibility_outcome;
    Freshness freshness = Freshness::UNKNOWN;
    Health health = Health::UNKNOWN;
    Evidence provenance = Evidence::UNKNOWN;
    std::string authority_boot;
    std::string source;

    std::vector<CandidateRanking> ranked_candidates;
    std::vector<EliminatedCandidate> eliminated;
    std::vector<std::string> notes;

    RetrievalEstimate retrieval;
    std::string semantic_digest;
    std::string summary;

    [[nodiscard]] bool found() const noexcept {
        return outcome == QueryOutcome::FOUND_EXACT ||
               outcome == QueryOutcome::FOUND_COMPATIBLE ||
               outcome == QueryOutcome::FOUND_MULTIPLE;
    }

    void serialize(ByteWriter& w) const {
        result_id.serialize(w);
        query_id.serialize(w);
        query_plan_id.serialize(w);
        query_generation.serialize(w);
        w.push_byte(static_cast<std::uint8_t>(outcome));
        w.push_byte(selected_record_id ? 1 : 0); if (selected_record_id) selected_record_id->serialize(w);
        w.push_byte(selected_generation ? 1 : 0); if (selected_generation) selected_generation->serialize(w);
        w.push_byte(selected_placement ? 1 : 0); if (selected_placement) selected_placement->serialize(w);
        w.push_byte(selected_state_id ? 1 : 0); if (selected_state_id) selected_state_id->serialize(w);
        w.push_string(compatibility_outcome);
        w.push_byte(static_cast<std::uint8_t>(freshness));
        w.push_byte(static_cast<std::uint8_t>(health));
        w.push_byte(static_cast<std::uint8_t>(provenance));
        w.push_string(authority_boot);
        w.push_string(source);
        w.push_u64(ranked_candidates.size());
        for (const auto& c : ranked_candidates) {
            c.record_id.serialize(w);
            c.generation.serialize(w);
            w.push_byte(c.best_placement ? 1 : 0); if (c.best_placement) c.best_placement->serialize(w);
            w.push_u64(c.factors.size());
            for (const auto& [f, v] : c.factors) {
                w.push_byte(static_cast<std::uint8_t>(f));
                std::uint64_t bits; std::memcpy(&bits, &v, sizeof(v)); w.push_u64(bits);
            }
            w.push_u64(c.factor_notes.size());
            for (const auto& s : c.factor_notes) w.push_string(s);
            w.push_string(c.summary);
        }
        w.push_u64(eliminated.size());
        for (const auto& e : eliminated) {
            e.record_id.serialize(w);
            w.push_byte(static_cast<std::uint8_t>(e.reason));
            w.push_string(e.note);
        }
        w.push_u64(notes.size());
        for (const auto& s : notes) w.push_string(s);
        retrieval.serialize(w);
        w.push_string(semantic_digest);
        w.push_string(summary);
    }
    static QueryResult deserialize(ByteReader& r) {
        QueryResult q;
        q.result_id = ResultId::deserialize(r);
        q.query_id = QueryId::deserialize(r);
        q.query_plan_id = QueryPlanId::deserialize(r);
        q.query_generation = QueryGeneration::deserialize(r);
        q.outcome = static_cast<QueryOutcome>(r.read_u8());
        if (r.read_u8()) q.selected_record_id = StateRecordId::deserialize(r);
        if (r.read_u8()) q.selected_generation = StateGeneration::deserialize(r);
        if (r.read_u8()) q.selected_placement = PlacementId::deserialize(r);
        if (r.read_u8()) q.selected_state_id = StateId::deserialize(r);
        q.compatibility_outcome = r.read_string();
        q.freshness = static_cast<Freshness>(r.read_u8());
        q.health = static_cast<Health>(r.read_u8());
        q.provenance = static_cast<Evidence>(r.read_u8());
        q.authority_boot = r.read_string();
        q.source = r.read_string();
        std::uint64_t nc = r.read_u64();
        if (nc > kMaxRecordCount) throw std::runtime_error("query result: candidate count exceeds bound");
        for (std::uint64_t i = 0; i < nc; ++i) {
            CandidateRanking c;
            c.record_id = StateRecordId::deserialize(r);
            c.generation = StateGeneration::deserialize(r);
            if (r.read_u8()) c.best_placement = PlacementId::deserialize(r);
            std::uint64_t nf = r.read_u64();
            if (nf > 64) throw std::runtime_error("query result: factor count exceeds bound");
            for (std::uint64_t j = 0; j < nf; ++j) {
                auto f = static_cast<RankingFactor>(r.read_u8());
                std::uint64_t bits = r.read_u64(); double v; std::memcpy(&v, &bits, sizeof(v));
                c.factors.emplace_back(f, v);
            }
            std::uint64_t nn = r.read_u64();
            if (nn > 64) throw std::runtime_error("query result: note count exceeds bound");
            for (std::uint64_t j = 0; j < nn; ++j) c.factor_notes.push_back(r.read_string());
            c.summary = r.read_string();
            q.ranked_candidates.push_back(std::move(c));
        }
        std::uint64_t ne = r.read_u64();
        if (ne > kMaxRecordCount) throw std::runtime_error("query result: eliminated count exceeds bound");
        for (std::uint64_t i = 0; i < ne; ++i) {
            EliminatedCandidate e;
            e.record_id = StateRecordId::deserialize(r);
            e.reason = static_cast<RejectionReason>(r.read_u8());
            e.note = r.read_string();
            q.eliminated.push_back(std::move(e));
        }
        std::uint64_t nn = r.read_u64();
        if (nn > kMaxStrings) throw std::runtime_error("query result: note count exceeds bound");
        for (std::uint64_t i = 0; i < nn; ++i) q.notes.push_back(r.read_string());
        q.retrieval = RetrievalEstimate::deserialize(r);
        q.semantic_digest = r.read_string();
        q.summary = r.read_string();
        return q;
    }
};

}  // namespace stateindex

#endif  // STATEINDEX_RESULT_HPP
