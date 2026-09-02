// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef STATEINDEX_SCENARIOS_HPP
#define STATEINDEX_SCENARIOS_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "stateindex/stateindex.hpp"

namespace siscenario {

using namespace stateindex;

inline std::uint64_t global_counter = 0;

inline StateId next_state() { return StateId(++global_counter); }
inline WorkerBootId next_boot() { return WorkerBootId(++global_counter + 2000000); }

// Build a default authority envelope for a live worker boot.
inline MutationEnvelope env(const CoordinatorEpoch& epoch, const WorkerBootId& boot,
                            const StateGeneration& gen = StateGeneration(),
                            const RecordGeneration& rec = RecordGeneration()) {
    MutationEnvelope e;
    e.coordinator_epoch = epoch;
    e.worker_boot = boot;
    e.attempt_generation = AttemptGeneration(1);
    e.dispatch_generation = DispatchGeneration(1);
    e.state_generation = gen;
    e.record_generation = rec;
    return e;
}

struct LocationSpec {
    MemoryDomain domain;
    NodeId node;
    DeviceId device;    // 0 = none
    Health health = Health::HEALTHY;
    Freshness freshness = Freshness::CURRENT;
    Evidence provenance = Evidence::SYNTHETIC;
    double latency_ms = 1.0;
    double bandwidth_mbps = 1000.0;
    double bytes = 1024.0;
    double restore = 0.0;
    std::string address;
};

inline StateLocation make_location(LocationSpec s, PlacementId placement, LocationGeneration gen) {
    StateLocation l;
    l.placement_id = placement;
    l.replica_id = ReplicaId(placement.value());
    l.node_id = s.node;
    l.device_id = s.device;
    l.domain_id = MemoryDomainId(static_cast<std::uint64_t>(s.domain));
    l.domain = s.domain;
    l.backend = (s.domain == MemoryDomain::CUDA_DEVICE) ? StorageBackend::CUDA
               : (s.domain == MemoryDomain::HOST_MEMORY || s.domain == MemoryDomain::HOST_PINNED) ? StorageBackend::MEMORY
               : (s.domain == MemoryDomain::LOCAL_FILESYSTEM || s.domain == MemoryDomain::LOCAL_NVME_CLASS) ? StorageBackend::FILE
               : StorageBackend::SYNTHETIC;
    l.tier = StorageTier::HOT;
    l.location_generation = gen;
    l.placement_generation = PlacementGeneration(gen.value());
    l.replica_generation = ReplicaGeneration(gen.value());
    l.address = s.address.empty() ? "addr://" + std::to_string(placement.value()) : s.address;
    l.byte_size = static_cast<std::uint64_t>(s.bytes);
    l.access_class = (s.domain == MemoryDomain::SYNTHETIC_REMOTE || s.domain == MemoryDomain::REMOTE_CACHE_CLASS ||
                      s.domain == MemoryDomain::OBJECT_STORAGE_CLASS) ? AccessClass::REMOTE : AccessClass::LOCAL;
    l.health = s.health;
    l.freshness = s.freshness;
    l.provenance = s.provenance;
    l.locality_metadata = "node=" + std::to_string(s.node.value());
    l.retrieval.expected_latency_ms = s.latency_ms;
    l.retrieval.expected_bandwidth_mbps = s.bandwidth_mbps;
    l.retrieval.transfer_bytes = s.bytes;
    l.retrieval.restore_cost = s.restore;
    l.retrieval.evidence = s.provenance;
    l.retrieval.local_access_cost = s.latency_ms;
    l.observation_generation = ObservationGeneration(1);
    l.provenance_generation = ProvenanceGeneration(1);
    return l;
}

struct RecordOpts {
    StateKind kind = StateKind::KV_STATE;
    NamespaceId ns = NamespaceId(1);
    OwnerId owner = OwnerId(1);
    ProducerId producer = ProducerId(1);
    std::uint64_t size = 1024;
    std::string digest;
    std::optional<CompatibilityRef> compat;
    std::optional<ProvenanceRef> prov;
    std::vector<DependencyRef> deps;
    std::vector<LocationSpec> locations;
    Lifecycle lifecycle = Lifecycle::AVAILABLE;
    Freshness freshness = Freshness::CURRENT;
    Health health = Health::HEALTHY;
};

// Deterministic record id derived from (state, generation).
inline StateRecordId record_id_of(StateId state, StateGeneration gen) {
    return StateRecordId((state.value() << 32) | gen.value());
}

inline StateRecord make_record(StateId state, StateGeneration gen, RecordOpts opts) {
    StateRecord r;
    r.record_id = record_id_of(state, gen);
    r.state_id = state;
    r.state_generation = gen;
    r.record_generation = RecordGeneration(gen.value());
    r.kind = opts.kind;
    r.namespace_id = opts.ns;
    r.owner_id = opts.owner;
    r.producer_id = opts.producer;
    r.logical_size = opts.size;
    r.content_digest = opts.digest;
    r.compatibility = opts.compat;
    r.provenance = opts.prov;
    r.dependencies = opts.deps;
    r.lifecycle = opts.lifecycle;
    r.freshness = opts.freshness;
    r.health = opts.health;
    r.policy_generation = PolicyGeneration(1);
    r.created_at = 1000;
    r.updated_at = 1000;
    r.description = "test record";
    for (std::size_t i = 0; i < opts.locations.size(); ++i)
        r.locations.push_back(make_location(opts.locations[i],
                                            PlacementId((state.value() << 24) | (gen.value() << 8) | (i + 1)),
                                            LocationGeneration(gen.value())));
    r.semantic_digest = r.compute_semantic_digest();
    return r;
}

}  // namespace siscenario

#endif  // STATEINDEX_SCENARIOS_HPP
