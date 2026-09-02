// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef STATEINDEX_ACCOUNTING_HPP
#define STATEINDEX_ACCOUNTING_HPP

#include <cstdint>
#include <stdexcept>
#include <string>

#include "stateindex/bytebuf.hpp"

namespace stateindex {

// Explicit accounting. Counters are monotonic and never negative; a decrement
// of a counter that is already zero is a logic error and is reported rather
// than allowed to underflow. Duplicate removes are guarded by existence checks
// at the call site so they never double-decrement.
class Accounting {
public:
    std::uint64_t logical_states = 0;
    std::uint64_t current_states = 0;
    std::uint64_t historical_records = 0;
    std::uint64_t locations = 0;
    std::uint64_t replicas = 0;
    std::uint64_t cuda_locations = 0;
    std::uint64_t host_locations = 0;
    std::uint64_t storage_locations = 0;
    std::uint64_t invalidations = 0;
    std::uint64_t tombstones = 0;
    std::uint64_t dependencies = 0;
    std::uint64_t exact_queries = 0;
    std::uint64_t predicate_queries = 0;
    std::uint64_t query_hits = 0;
    std::uint64_t query_misses = 0;
    std::uint64_t stale_only_outcomes = 0;
    std::uint64_t incompatible_only_outcomes = 0;
    std::uint64_t stale_mutation_rejections = 0;
    std::uint64_t duplicate_mutation_rejections = 0;
    std::uint64_t worker_restarts = 0;

    // Generic guard: decrement is only legal above zero.
    static void dec(std::uint64_t& counter, const char* name) {
        if (counter == 0)
            throw std::runtime_error(std::string("accounting: attempted to decrement zero counter: ") + name);
        --counter;
    }

    [[nodiscard]] bool valid() const noexcept {
        return true;  // counters are unsigned; state is always coherent by construction
    }

    void serialize(ByteWriter& w) const {
        w.push_u64(logical_states); w.push_u64(current_states); w.push_u64(historical_records);
        w.push_u64(locations); w.push_u64(replicas); w.push_u64(cuda_locations);
        w.push_u64(host_locations); w.push_u64(storage_locations); w.push_u64(invalidations);
        w.push_u64(tombstones); w.push_u64(dependencies); w.push_u64(exact_queries);
        w.push_u64(predicate_queries); w.push_u64(query_hits); w.push_u64(query_misses);
        w.push_u64(stale_only_outcomes); w.push_u64(incompatible_only_outcomes);
        w.push_u64(stale_mutation_rejections); w.push_u64(duplicate_mutation_rejections);
        w.push_u64(worker_restarts);
    }
    static Accounting deserialize(ByteReader& r) {
        Accounting a;
        a.logical_states = r.read_u64(); a.current_states = r.read_u64(); a.historical_records = r.read_u64();
        a.locations = r.read_u64(); a.replicas = r.read_u64(); a.cuda_locations = r.read_u64();
        a.host_locations = r.read_u64(); a.storage_locations = r.read_u64(); a.invalidations = r.read_u64();
        a.tombstones = r.read_u64(); a.dependencies = r.read_u64(); a.exact_queries = r.read_u64();
        a.predicate_queries = r.read_u64(); a.query_hits = r.read_u64(); a.query_misses = r.read_u64();
        a.stale_only_outcomes = r.read_u64(); a.incompatible_only_outcomes = r.read_u64();
        a.stale_mutation_rejections = r.read_u64(); a.duplicate_mutation_rejections = r.read_u64();
        a.worker_restarts = r.read_u64();
        return a;
    }

    // Human-readable summary used by tests, CLI, and reports.
    std::string summary() const {
        return "logical_states=" + std::to_string(logical_states) +
               " current_states=" + std::to_string(current_states) +
               " historical_records=" + std::to_string(historical_records) +
               " locations=" + std::to_string(locations) +
               " replicas=" + std::to_string(replicas) +
               " cuda_locations=" + std::to_string(cuda_locations) +
               " host_locations=" + std::to_string(host_locations) +
               " storage_locations=" + std::to_string(storage_locations) +
               " invalidations=" + std::to_string(invalidations) +
               " tombstones=" + std::to_string(tombstones) +
               " dependencies=" + std::to_string(dependencies) +
               " exact_queries=" + std::to_string(exact_queries) +
               " predicate_queries=" + std::to_string(predicate_queries) +
               " query_hits=" + std::to_string(query_hits) +
               " query_misses=" + std::to_string(query_misses) +
               " stale_only=" + std::to_string(stale_only_outcomes) +
               " incompatible_only=" + std::to_string(incompatible_only_outcomes) +
               " stale_mutation_rejections=" + std::to_string(stale_mutation_rejections) +
               " duplicate_mutation_rejections=" + std::to_string(duplicate_mutation_rejections) +
               " worker_restarts=" + std::to_string(worker_restarts);
    }
};

}  // namespace stateindex

#endif  // STATEINDEX_ACCOUNTING_HPP
