// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef STATEINDEX_AUTHORITY_HPP
#define STATEINDEX_AUTHORITY_HPP

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "stateindex/strong.hpp"
#include "stateindex/enums.hpp"

namespace stateindex {

// The mutation envelope carries every authority token a distributed mutation
// must present. A mutation missing or carrying a stale token is rejected
// before any state change.
struct MutationEnvelope {
    CoordinatorEpoch coordinator_epoch{};
    WorkerId worker_id{};
    WorkerBootId worker_boot{};
    AttemptGeneration attempt_generation{};
    DispatchGeneration dispatch_generation{};

    StateGeneration state_generation{};
    RecordGeneration record_generation{};
    OwnerGeneration owner_generation{};
    ProducerGeneration producer_generation{};
    WorkerGeneration worker_generation{};
    LocationGeneration location_generation{};
    PlacementGeneration placement_generation{};
    ReplicaGeneration replica_generation{};
    CompatibilityGeneration compatibility_generation{};
    ProvenanceGeneration provenance_generation{};
    DependencyGeneration dependency_generation{};
    ObservationGeneration observation_generation{};
    PolicyGeneration policy_generation{};

    void serialize(ByteWriter& w) const {
        coordinator_epoch.serialize(w);
        worker_id.serialize(w);
        worker_boot.serialize(w);
        attempt_generation.serialize(w);
        dispatch_generation.serialize(w);
        state_generation.serialize(w);
        record_generation.serialize(w);
        owner_generation.serialize(w);
        producer_generation.serialize(w);
        worker_generation.serialize(w);
        location_generation.serialize(w);
        placement_generation.serialize(w);
        replica_generation.serialize(w);
        compatibility_generation.serialize(w);
        provenance_generation.serialize(w);
        dependency_generation.serialize(w);
        observation_generation.serialize(w);
        policy_generation.serialize(w);
    }
    static MutationEnvelope deserialize(ByteReader& r) {
        MutationEnvelope e;
        e.coordinator_epoch = CoordinatorEpoch::deserialize(r);
        e.worker_id = WorkerId::deserialize(r);
        e.worker_boot = WorkerBootId::deserialize(r);
        e.attempt_generation = AttemptGeneration::deserialize(r);
        e.dispatch_generation = DispatchGeneration::deserialize(r);
        e.state_generation = StateGeneration::deserialize(r);
        e.record_generation = RecordGeneration::deserialize(r);
        e.owner_generation = OwnerGeneration::deserialize(r);
        e.producer_generation = ProducerGeneration::deserialize(r);
        e.worker_generation = WorkerGeneration::deserialize(r);
        e.location_generation = LocationGeneration::deserialize(r);
        e.placement_generation = PlacementGeneration::deserialize(r);
        e.replica_generation = ReplicaGeneration::deserialize(r);
        e.compatibility_generation = CompatibilityGeneration::deserialize(r);
        e.provenance_generation = ProvenanceGeneration::deserialize(r);
        e.dependency_generation = DependencyGeneration::deserialize(r);
        e.observation_generation = ObservationGeneration::deserialize(r);
        e.policy_generation = PolicyGeneration::deserialize(r);
        return e;
    }
};

// Outcome of validating a mutation against current authority.
enum class MutationVerdict : std::uint8_t {
    ACCEPTED = 0,
    REJECTED_STALE_EPOCH = 1,
    REJECTED_STALE_BOOT = 2,
    REJECTED_GENERATION_REGRESSION = 3,
    REJECTED_DUPLICATE = 4,
    REJECTED_TOMBSTONED = 5,
    REJECTED_NOT_LIVE = 6,
    REJECTED_NON_MONOTONIC = 7,
};

inline std::string to_string(MutationVerdict v) {
    switch (v) {
        case MutationVerdict::ACCEPTED: return "ACCEPTED";
        case MutationVerdict::REJECTED_STALE_EPOCH: return "REJECTED_STALE_EPOCH";
        case MutationVerdict::REJECTED_STALE_BOOT: return "REJECTED_STALE_BOOT";
        case MutationVerdict::REJECTED_GENERATION_REGRESSION: return "REJECTED_GENERATION_REGRESSION";
        case MutationVerdict::REJECTED_DUPLICATE: return "REJECTED_DUPLICATE";
        case MutationVerdict::REJECTED_TOMBSTONED: return "REJECTED_TOMBSTONED";
        case MutationVerdict::REJECTED_NOT_LIVE: return "REJECTED_NOT_LIVE";
        case MutationVerdict::REJECTED_NON_MONOTONIC: return "REJECTED_NON_MONOTONIC";
    }
    return "UNKNOWN";
}

struct MutationVerdictDetail {
    MutationVerdict verdict = MutationVerdict::REJECTED_NOT_LIVE;
    std::string reason;
};

// Per-subject monotonic high-water marker, scoped to one boot incarnation.
struct BootHighWater {
    WorkerBootId boot;
    StateGeneration state_gen{};
    RecordGeneration record_gen{};
    LocationGeneration location_gen{};
    PlacementGeneration placement_gen{};
    ReplicaGeneration replica_gen{};
    CompatibilityGeneration compatibility_gen{};
    ProvenanceGeneration provenance_gen{};
    DependencyGeneration dependency_gen{};
};

// The AuthorityRegistry is the single place that decides whether a mutation is
// stale. It owns the coordinator epoch, the set of live worker incarnations,
// each incarnation's high-water marks, and the current authoritative
// generation for each StateId.
//
// Incarnation scoping rule: generation comparisons are only made within the
// same boot incarnation. A larger generation published by an old (no longer
// live) WorkerBootId never fences a fresh process incarnation.
class AuthorityRegistry {
public:
    explicit AuthorityRegistry(CoordinatorEpoch epoch = CoordinatorEpoch(1)) : epoch_(epoch) {}

    CoordinatorEpoch epoch() const noexcept { return epoch_; }
    void advance_epoch() { epoch_ = epoch_.next(); }
    void set_epoch(CoordinatorEpoch e) { epoch_ = e; }
    void clear_live_workers() { live_boots_.clear(); worker_incarnation_.clear(); }

    void register_worker(WorkerId worker, WorkerBootId boot) {
        // A fresh boot is always meaningful. If the worker already had a live
        // boot, the new boot supersedes it as the worker's current incarnation.
        live_boots_[to_key(boot)] = worker;
        worker_incarnation_[to_key(worker)] = boot;
        // Wipe any high-water marks carried from a previous incarnation so a
        // stale incarnation-local generation cannot fence the fresh boot.
        boot_high_water_.erase(to_key(boot));
    }

    void unregister_worker(WorkerBootId boot) {
        auto it = live_boots_.find(to_key(boot));
        if (it != live_boots_.end()) live_boots_.erase(it);
    }

    bool is_live(const WorkerBootId& boot) const noexcept {
        return live_boots_.count(to_key(boot)) != 0;
    }

    std::optional<WorkerBootId> current_boot_of(WorkerId worker) const {
        auto it = worker_incarnation_.find(to_key(worker));
        if (it == worker_incarnation_.end()) return std::nullopt;
        return it->second;
    }

    // The floor generation forced by a current tombstone for a StateId.
    void set_tombstone_floor(const StateId& state, const StateGeneration& floor) {
        tombstone_floor_[to_key(state)] = floor;
    }
    std::optional<StateGeneration> tombstone_floor(const StateId& state) const {
        auto it = tombstone_floor_.find(to_key(state));
        if (it == tombstone_floor_.end()) return std::nullopt;
        return it->second;
    }

    // Record the current authoritative generation and which incarnation owns
    // it. Returns true if the caller may treat this as the current authority.
    void set_current_authority(const StateId& state, const StateGeneration& gen, const WorkerBootId& boot) {
        current_authority_[to_key(state)] = {gen, boot};
    }
    void clear_current_authority(const StateId& state) {
        current_authority_.erase(to_key(state));
    }
    std::optional<std::pair<StateGeneration, WorkerBootId>> current_authority(const StateId& state) const {
        auto it = current_authority_.find(to_key(state));
        if (it == current_authority_.end()) return std::nullopt;
        return it->second;
    }

    // Validate a state-publication envelope for a given StateId under the
    // "only current generation is authoritative" rule.
    MutationVerdictDetail validate_state_publication(const StateId& state,
                                                     const MutationEnvelope& env,
                                                     StateGeneration new_state_gen) const;

    // Validate a location update for a given placement under this boot.
    static bool monotonic(const LocationGeneration& candidate, const LocationGeneration& prior) noexcept {
        return candidate > prior;
    }

private:
    CoordinatorEpoch epoch_;
    std::unordered_map<std::uint64_t, WorkerId> live_boots_;
    std::unordered_map<std::uint64_t, WorkerBootId> worker_incarnation_;
    std::unordered_map<std::uint64_t, BootHighWater> boot_high_water_;
    std::unordered_map<std::uint64_t, StateGeneration> tombstone_floor_;
    std::unordered_map<std::uint64_t, std::pair<StateGeneration, WorkerBootId>> current_authority_;

    static std::uint64_t to_key(StateId s) { return s.value(); }
    static std::uint64_t to_key(WorkerId w) { return w.value(); }
    static std::uint64_t to_key(WorkerBootId b) { return b.value(); }
};

}  // namespace stateindex

#endif  // STATEINDEX_AUTHORITY_HPP
