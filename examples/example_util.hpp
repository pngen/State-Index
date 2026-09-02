// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef STATEINDEX_EXAMPLE_UTIL_HPP
#define STATEINDEX_EXAMPLE_UTIL_HPP

#include <cstdint>
#include <iostream>
#include <string>
#include "stateindex/stateindex.hpp"

namespace exutil {
using namespace stateindex;

inline std::uint64_t boot = 5000;

inline void ensure_worker(StateIndexEngine& eng) {
    if (!eng.authority().is_live(WorkerBootId(boot))) eng.register_worker(WorkerId(1), WorkerBootId(boot));
}

inline MutationEnvelope env(const CoordinatorEpoch& e, const StateGeneration& g = StateGeneration()) {
    MutationEnvelope m;
    m.coordinator_epoch = e;
    m.worker_boot = WorkerBootId(boot);
    m.attempt_generation = AttemptGeneration(1);
    m.dispatch_generation = DispatchGeneration(1);
    m.state_generation = g;
    return m;
}

inline StateLocation loc(const StateId& st, const StateGeneration& gen, const std::string& domain,
                         std::uint64_t node, std::uint64_t device = 0) {
    StateLocation l;
    l.placement_id = PlacementId((st.value() << 24) | (gen.value() << 8) | 1);
    l.replica_id = ReplicaId(l.placement_id.value());
    l.node_id = NodeId(node);
    l.device_id = device ? DeviceId(device) : DeviceId();
    l.domain = domain == "cuda" ? MemoryDomain::CUDA_DEVICE
             : domain == "file" ? MemoryDomain::LOCAL_FILESYSTEM
             : domain == "remote" ? MemoryDomain::SYNTHETIC_REMOTE : MemoryDomain::HOST_MEMORY;
    l.backend = l.domain == MemoryDomain::CUDA_DEVICE ? StorageBackend::CUDA
               : l.domain == MemoryDomain::LOCAL_FILESYSTEM ? StorageBackend::FILE : StorageBackend::MEMORY;
    l.tier = StorageTier::HOT;
    l.location_generation = LocationGeneration(gen.value());
    l.placement_generation = PlacementGeneration(gen.value());
    l.replica_generation = ReplicaGeneration(gen.value());
    l.address = "ex://" + std::to_string(l.placement_id.value());
    l.byte_size = 1024;
    l.access_class = l.domain == MemoryDomain::SYNTHETIC_REMOTE ? AccessClass::REMOTE : AccessClass::LOCAL;
    l.health = Health::HEALTHY;
    l.freshness = Freshness::CURRENT;
    l.provenance = l.domain == MemoryDomain::SYNTHETIC_REMOTE ? Evidence::SYNTHETIC : Evidence::MEASURED;
    l.retrieval.expected_latency_ms = l.domain == MemoryDomain::SYNTHETIC_REMOTE ? 50.0 : 1.0;
    l.retrieval.evidence = l.provenance;
    l.authority_epoch = CoordinatorEpoch(1);
    l.authority_boot = WorkerBootId(boot);
    return l;
}

inline StateRecord rec(const StateId& st, const StateGeneration& gen, const std::string& kind = "kv",
                       const std::string& domain = "mem", std::uint64_t node = 1, std::uint64_t device = 0,
                       std::optional<CompatibilityRef> compat = std::nullopt,
                       std::vector<DependencyRef> deps = {}) {
    StateRecord r;
    r.record_id = StateRecordId((st.value() << 32) | gen.value());
    r.state_id = st;
    r.state_generation = gen;
    r.record_generation = RecordGeneration(gen.value());
    r.kind = kind == "tensor" ? StateKind::TENSOR_STATE
           : kind == "artifact" ? StateKind::MODEL_ARTIFACT
           : kind == "kernel" ? StateKind::COMPILED_KERNEL : StateKind::KV_STATE;
    r.namespace_id = NamespaceId(1);
    r.owner_id = OwnerId(1);
    r.producer_id = ProducerId(1);
    r.logical_size = 1024;
    r.content_digest = kind == "kv" ? "d" + std::to_string(st.value()) : "";
    r.compatibility = compat;
    r.dependencies = deps;
    r.lifecycle = Lifecycle::AVAILABLE;
    r.freshness = Freshness::CURRENT;
    r.health = Health::HEALTHY;
    r.policy_generation = PolicyGeneration(1);
    r.created_at = 1000; r.updated_at = 1000;
    r.description = "example record";
    r.locations.push_back(loc(st, gen, domain, node, device));
    r.semantic_digest = r.compute_semantic_digest();
    return r;
}

}  // namespace exutil

#endif
