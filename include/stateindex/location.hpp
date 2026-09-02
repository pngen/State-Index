// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef STATEINDEX_LOCATION_HPP
#define STATEINDEX_LOCATION_HPP

#include <cstdint>
#include <optional>
#include <string>

#include "stateindex/strong.hpp"
#include "stateindex/enums.hpp"
#include "stateindex/cost.hpp"
#include "stateindex/bytebuf.hpp"

namespace stateindex {

// A StateLocation names where a reusable-state copy physically or logically
// lives. State identity and physical location are separate. A raw pointer is
// never persisted as cross-process authority; process-scoped handles are
// optionally recorded only for local debugging and are invalidated on recovery.
struct StateLocation {
    PlacementId placement_id{};
    ReplicaId replica_id{};
    NodeId node_id{};
    DeviceId device_id{};
    MemoryDomainId domain_id{};
    MemoryDomain domain = MemoryDomain::UNKNOWN;
    StorageBackend backend = StorageBackend::UNKNOWN;
    StorageTier tier = StorageTier::UNKNOWN;
    LocationGeneration location_generation{};
    PlacementGeneration placement_generation{};
    ReplicaGeneration replica_generation{};
    std::string address;   // key/path/address abstraction
    std::uint64_t byte_size = 0;
    AccessClass access_class = AccessClass::UNKNOWN;
    Health health = Health::UNKNOWN;
    Freshness freshness = Freshness::UNKNOWN;
    Evidence provenance = Evidence::SYNTHETIC;
    std::string locality_metadata;
    RetrievalEstimate retrieval;
    ObservationGeneration observation_generation{};
    ProvenanceGeneration provenance_generation{};
    CoordinatorEpoch authority_epoch{};
    WorkerBootId authority_boot{};
    std::int64_t created_at = 0;
    std::int64_t updated_at = 0;

    // Process-scoped, non-persisted debug handle. Never serialized.
    std::optional<std::uint64_t> process_handle;
    std::optional<WorkerBootId> process_scope;

    [[nodiscard]] bool usable() const noexcept {
        return health == Health::HEALTHY || health == Health::DEGRADED;
    }

    void serialize(ByteWriter& w) const {
        placement_id.serialize(w);
        replica_id.serialize(w);
        node_id.serialize(w);
        device_id.serialize(w);
        domain_id.serialize(w);
        w.push_byte(static_cast<std::uint8_t>(domain));
        w.push_byte(static_cast<std::uint8_t>(backend));
        w.push_byte(static_cast<std::uint8_t>(tier));
        location_generation.serialize(w);
        placement_generation.serialize(w);
        replica_generation.serialize(w);
        w.push_string(address);
        w.push_u64(byte_size);
        w.push_byte(static_cast<std::uint8_t>(access_class));
        w.push_byte(static_cast<std::uint8_t>(health));
        w.push_byte(static_cast<std::uint8_t>(freshness));
        w.push_byte(static_cast<std::uint8_t>(provenance));
        w.push_string(locality_metadata);
        retrieval.serialize(w);
        observation_generation.serialize(w);
        provenance_generation.serialize(w);
        authority_epoch.serialize(w);
        authority_boot.serialize(w);
        w.push_i64(created_at);
        w.push_i64(updated_at);
    }
    static StateLocation deserialize(ByteReader& r) {
        StateLocation l;
        l.placement_id = PlacementId::deserialize(r);
        l.replica_id = ReplicaId::deserialize(r);
        l.node_id = NodeId::deserialize(r);
        l.device_id = DeviceId::deserialize(r);
        l.domain_id = MemoryDomainId::deserialize(r);
        l.domain = static_cast<MemoryDomain>(r.read_u8());
        l.backend = static_cast<StorageBackend>(r.read_u8());
        l.tier = static_cast<StorageTier>(r.read_u8());
        l.location_generation = LocationGeneration::deserialize(r);
        l.placement_generation = PlacementGeneration::deserialize(r);
        l.replica_generation = ReplicaGeneration::deserialize(r);
        l.address = r.read_string();
        l.byte_size = r.read_u64();
        l.access_class = static_cast<AccessClass>(r.read_u8());
        l.health = static_cast<Health>(r.read_u8());
        l.freshness = static_cast<Freshness>(r.read_u8());
        l.provenance = static_cast<Evidence>(r.read_u8());
        l.locality_metadata = r.read_string();
        l.retrieval = RetrievalEstimate::deserialize(r);
        l.observation_generation = ObservationGeneration::deserialize(r);
        l.provenance_generation = ProvenanceGeneration::deserialize(r);
        l.authority_epoch = CoordinatorEpoch::deserialize(r);
        l.authority_boot = WorkerBootId::deserialize(r);
        l.created_at = r.read_i64();
        l.updated_at = r.read_i64();
        return l;
    }
};

}  // namespace stateindex

#endif  // STATEINDEX_LOCATION_HPP
