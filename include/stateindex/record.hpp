// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef STATEINDEX_RECORD_HPP
#define STATEINDEX_RECORD_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "stateindex/strong.hpp"
#include "stateindex/enums.hpp"
#include "stateindex/refs.hpp"
#include "stateindex/location.hpp"
#include "stateindex/bytebuf.hpp"
#include "stateindex/digest.hpp"
#include "stateindex/limits.hpp"

namespace stateindex {

// Guarded lifecycle transitions. A record cannot become AVAILABLE merely
// because an index entry exists; availability requires current authority and at
// least one usable location (enforced by the engine). This function only guards
// the lifecycle enum transitions themselves.
inline bool can_transition(Lifecycle from, Lifecycle to) noexcept {
    if (from == to) return true;
    // Terminal states are never re-entered.
    if (from == Lifecycle::TOMBSTONED || from == Lifecycle::RETIRED) {
        return to == from;
    }
    switch (to) {
        case Lifecycle::DISCOVERED: return from == Lifecycle::FAILED;
        case Lifecycle::AVAILABLE: return from == Lifecycle::DISCOVERED || from == Lifecycle::DEGRADED;
        case Lifecycle::DEGRADED: return from == Lifecycle::AVAILABLE || from == Lifecycle::DISCOVERED;
        case Lifecycle::STALE: return from == Lifecycle::AVAILABLE || from == Lifecycle::DEGRADED || from == Lifecycle::DISCOVERED;
        case Lifecycle::INVALIDATED: return true;  // any non-terminal may be invalidated
        case Lifecycle::SUPERSEDED: return true;   // supersession replaces current authority
        case Lifecycle::TOMBSTONED: return true;   // any non-terminal may be tombstoned
        case Lifecycle::RETIRED: return true;
        case Lifecycle::MISSING: return true;
        case Lifecycle::FAILED: return true;
    }
    return false;
}

// A StateRecord is the canonical logical descriptor of one reusable state
// generation. Identity and physical location are separate; StateRecord never
// stores a raw cross-process pointer.
struct StateRecord {
    StateRecordId record_id{};
    StateId state_id{};
    StateGeneration state_generation{};
    RecordGeneration record_generation{};
    StateKind kind = StateKind::GENERIC_STATE;
    NamespaceId namespace_id{};
    OwnerId owner_id{};
    ProducerId producer_id{};
    std::uint64_t logical_size = 0;
    std::string content_digest;     // hex; empty means unknown
    std::optional<CompatibilityRef> compatibility;
    std::optional<ProvenanceRef> provenance;
    std::vector<DependencyRef> dependencies;
    std::vector<StateLocation> locations;
    Lifecycle lifecycle = Lifecycle::DISCOVERED;
    Freshness freshness = Freshness::UNKNOWN;
    Health health = Health::UNKNOWN;
    PolicyGeneration policy_generation{};
    CoordinatorEpoch authority_epoch{};
    WorkerBootId authority_boot{};
    std::int64_t created_at = 0;
    std::int64_t updated_at = 0;
    // Which generation this record superseded (0 = none).
    StateGeneration supersedes_generation{};
    std::string description;
    // Stored semantic digest (SHA-256 hex). Recomputed on load and verified.
    std::string semantic_digest;
    // Non-persisted; marks a historical replica.
    bool is_historical = false;

    // Serialize the full record including the stored semantic digest.
    void serialize(ByteWriter& w) const {
        record_id.serialize(w);
        state_id.serialize(w);
        state_generation.serialize(w);
        record_generation.serialize(w);
        w.push_u16(static_cast<std::uint16_t>(kind));
        namespace_id.serialize(w);
        owner_id.serialize(w);
        producer_id.serialize(w);
        w.push_u64(logical_size);
        w.push_string(content_digest);
        w.push_byte(compatibility ? 1 : 0);
        if (compatibility) compatibility->serialize(w);
        w.push_byte(provenance ? 1 : 0);
        if (provenance) provenance->serialize(w);
        w.push_u64(dependencies.size());
        for (const auto& d : dependencies) d.serialize(w);
        w.push_u64(locations.size());
        for (const auto& l : locations) l.serialize(w);
        w.push_byte(static_cast<std::uint8_t>(lifecycle));
        w.push_byte(static_cast<std::uint8_t>(freshness));
        w.push_byte(static_cast<std::uint8_t>(health));
        policy_generation.serialize(w);
        authority_epoch.serialize(w);
        authority_boot.serialize(w);
        w.push_i64(created_at);
        w.push_i64(updated_at);
        supersedes_generation.serialize(w);
        w.push_string(description);
        w.push_string(semantic_digest);
    }

    // Serialize everything except the stored semantic digest. Used to
    // recompute and verify the digest.
    ByteWriter canonical_without_digest() const {
        ByteWriter w;
        StateRecord tmp = *this;
        const std::string saved = tmp.semantic_digest;
        tmp.semantic_digest.clear();
        tmp.serialize(w);
        tmp.semantic_digest = saved;
        return w;
    }

    static StateRecord deserialize(ByteReader& r) {
        StateRecord rec;
        rec.record_id = StateRecordId::deserialize(r);
        rec.state_id = StateId::deserialize(r);
        rec.state_generation = StateGeneration::deserialize(r);
        rec.record_generation = RecordGeneration::deserialize(r);
        rec.kind = static_cast<StateKind>(r.read_u16());
        rec.namespace_id = NamespaceId::deserialize(r);
        rec.owner_id = OwnerId::deserialize(r);
        rec.producer_id = ProducerId::deserialize(r);
        rec.logical_size = r.read_u64();
        rec.content_digest = r.read_string();
        if (r.read_u8() != 0) rec.compatibility = CompatibilityRef::deserialize(r);
        if (r.read_u8() != 0) rec.provenance = ProvenanceRef::deserialize(r);
        std::uint64_t ndep = r.read_u64();
        if (ndep > kMaxDependenciesPerRecord) throw std::runtime_error("record: dependency count exceeds bound");
        for (std::uint64_t i = 0; i < ndep; ++i) rec.dependencies.push_back(DependencyRef::deserialize(r));
        std::uint64_t nloc = r.read_u64();
        if (nloc > kMaxLocationsPerRecord) throw std::runtime_error("record: location count exceeds bound");
        for (std::uint64_t i = 0; i < nloc; ++i) rec.locations.push_back(StateLocation::deserialize(r));
        rec.lifecycle = static_cast<Lifecycle>(r.read_u8());
        rec.freshness = static_cast<Freshness>(r.read_u8());
        rec.health = static_cast<Health>(r.read_u8());
        rec.policy_generation = PolicyGeneration::deserialize(r);
        rec.authority_epoch = CoordinatorEpoch::deserialize(r);
        rec.authority_boot = WorkerBootId::deserialize(r);
        rec.created_at = r.read_i64();
        rec.updated_at = r.read_i64();
        rec.supersedes_generation = StateGeneration::deserialize(r);
        rec.description = r.read_string();
        rec.semantic_digest = r.read_string();
        return rec;
    }

    // Recompute the semantic digest from the canonical byte form.
    [[nodiscard]] std::string compute_semantic_digest() const {
        ByteWriter w = canonical_without_digest();
        return sha256_hex(w.data().data(), w.size());
    }

    [[nodiscard]] bool has_usable_location() const noexcept {
        for (const auto& l : locations)
            if (l.usable() && (l.freshness != Freshness::STALE)) return true;
        return false;
    }
};

}  // namespace stateindex

#endif  // STATEINDEX_RECORD_HPP
