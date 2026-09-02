// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef STATEINDEX_INVALIDATION_HPP
#define STATEINDEX_INVALIDATION_HPP

#include <cstdint>
#include <string>

#include "stateindex/strong.hpp"
#include "stateindex/enums.hpp"
#include "stateindex/bytebuf.hpp"

namespace stateindex {

// Invalidation does not rewrite historical records. It changes current-query
// eligibility while a historical record remains inspectable. A stale
// invalidation must never invalidate fresher state.
struct InvalidationRecord {
    std::uint64_t invalidation_seq = 0;   // monotonically increasing, persisted order
    InvalidationId invalidation_id{};
    StateId state_id{};
    StateGeneration bound_generation{};   // invalidates generations <= bound
    CoordinatorEpoch coordinator_epoch{};
    WorkerBootId worker_boot{};
    RecordGeneration record_generation{};
    LocationGeneration location_generation{};
    CompatibilityGeneration compatibility_generation{};
    ProvenanceGeneration provenance_generation{};
    DependencyGeneration dependency_generation{};
    PolicyGeneration policy_generation{};
    std::string reason;
    std::int64_t timestamp = 0;
    Evidence evidence = Evidence::MEASURED;
    bool current = true;   // false for historical invalidations

    void serialize(ByteWriter& w) const {
        w.push_u64(invalidation_seq);
        invalidation_id.serialize(w);
        state_id.serialize(w);
        bound_generation.serialize(w);
        coordinator_epoch.serialize(w);
        worker_boot.serialize(w);
        record_generation.serialize(w);
        location_generation.serialize(w);
        compatibility_generation.serialize(w);
        provenance_generation.serialize(w);
        dependency_generation.serialize(w);
        policy_generation.serialize(w);
        w.push_string(reason);
        w.push_i64(timestamp);
        w.push_byte(static_cast<std::uint8_t>(evidence));
        w.push_byte(current ? 1 : 0);
    }
    static InvalidationRecord deserialize(ByteReader& r) {
        InvalidationRecord inv;
        inv.invalidation_seq = r.read_u64();
        inv.invalidation_id = InvalidationId::deserialize(r);
        inv.state_id = StateId::deserialize(r);
        inv.bound_generation = StateGeneration::deserialize(r);
        inv.coordinator_epoch = CoordinatorEpoch::deserialize(r);
        inv.worker_boot = WorkerBootId::deserialize(r);
        inv.record_generation = RecordGeneration::deserialize(r);
        inv.location_generation = LocationGeneration::deserialize(r);
        inv.compatibility_generation = CompatibilityGeneration::deserialize(r);
        inv.provenance_generation = ProvenanceGeneration::deserialize(r);
        inv.dependency_generation = DependencyGeneration::deserialize(r);
        inv.policy_generation = PolicyGeneration::deserialize(r);
        inv.reason = r.read_string();
        inv.timestamp = r.read_i64();
        inv.evidence = static_cast<Evidence>(r.read_u8());
        inv.current = r.read_u8() != 0;
        return inv;
    }
};

// A tombstone binds a StateId, a minimum/current generation, and the authority
// that issued it. A stale producer cannot republish generations <= the
// tombstoned generation as current.
struct TombstoneRecord {
    TombstoneId tombstone_id{};
    StateId state_id{};
    StateGeneration floor_generation{};   // republishing <= floor is rejected
    CoordinatorEpoch coordinator_epoch{};
    WorkerBootId worker_boot{};
    std::string reason;
    std::int64_t timestamp = 0;
    Evidence evidence = Evidence::MEASURED;
    bool current = true;

    void serialize(ByteWriter& w) const {
        tombstone_id.serialize(w);
        state_id.serialize(w);
        floor_generation.serialize(w);
        coordinator_epoch.serialize(w);
        worker_boot.serialize(w);
        w.push_string(reason);
        w.push_i64(timestamp);
        w.push_byte(static_cast<std::uint8_t>(evidence));
        w.push_byte(current ? 1 : 0);
    }
    static TombstoneRecord deserialize(ByteReader& r) {
        TombstoneRecord t;
        t.tombstone_id = TombstoneId::deserialize(r);
        t.state_id = StateId::deserialize(r);
        t.floor_generation = StateGeneration::deserialize(r);
        t.coordinator_epoch = CoordinatorEpoch::deserialize(r);
        t.worker_boot = WorkerBootId::deserialize(r);
        t.reason = r.read_string();
        t.timestamp = r.read_i64();
        t.evidence = static_cast<Evidence>(r.read_u8());
        t.current = r.read_u8() != 0;
        return t;
    }

    [[nodiscard]] bool covers(const StateGeneration& gen) const noexcept {
        return current && gen.is_set() && gen <= floor_generation;
    }
};

}  // namespace stateindex

#endif  // STATEINDEX_INVALIDATION_HPP
