// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "test_util.hpp"
#include "scenarios.hpp"

using namespace stateindex;
using namespace siscenario;

static void lifecycle_guards() {
    CHECK(can_transition(Lifecycle::DISCOVERED, Lifecycle::AVAILABLE));
    CHECK(can_transition(Lifecycle::AVAILABLE, Lifecycle::DEGRADED));
    CHECK(!can_transition(Lifecycle::TOMBSTONED, Lifecycle::AVAILABLE));
    CHECK(!can_transition(Lifecycle::RETIRED, Lifecycle::AVAILABLE));
    CHECK(can_transition(Lifecycle::AVAILABLE, Lifecycle::INVALIDATED));
    CHECK(can_transition(Lifecycle::DEGRADED, Lifecycle::SUPERSEDED));
}

static void record_basic() {
    RecordOpts o;
    o.locations = { {MemoryDomain::HOST_MEMORY, NodeId(1), DeviceId()} };
    StateRecord r = make_record(StateId(5), StateGeneration(3), o);
    CHECK(r.state_id == StateId(5));
    CHECK(r.state_generation == StateGeneration(3));
    CHECK(r.kind == StateKind::KV_STATE);
    CHECK(r.locations.size() == 1);
    CHECK(r.locations[0].domain == MemoryDomain::HOST_MEMORY);
    CHECK(r.has_usable_location());
    CHECK(!r.semantic_digest.empty());
    // Semantic digest must be stable and verifiable.
    CHECK(r.compute_semantic_digest() == r.semantic_digest);
}

static void record_serialize_roundtrip() {
    RecordOpts o;
    o.locations = { {MemoryDomain::LOCAL_FILESYSTEM, NodeId(1), DeviceId()},
                    {MemoryDomain::CUDA_DEVICE, NodeId(1), DeviceId(3)} };
    o.compat = CompatibilityRef{CompatibilityId(9), CompatibilityGeneration(2),
                                CompatibilityOutcome::COMPATIBLE, Freshness::CURRENT,
                                Evidence::MEASURED, "test"};
    o.prov = ProvenanceRef{ProvenanceId(7), ProvenanceGeneration(1), ProducerId(2),
                           StateGeneration(1), "aabb", Evidence::MEASURED, Freshness::CURRENT};
    o.deps = { DependencyRef{DependencyId(4), DependencyGeneration(1), DependencyKind::STATE,
                             StateId(10), StateGeneration(1), true} };
    StateRecord r = make_record(StateId(20), StateGeneration(1), o);
    ByteWriter w;
    r.serialize(w);
    ByteReader rd(ByteSpan(w.data()));
    StateRecord r2 = StateRecord::deserialize(rd);
    CHECK(r2.record_id == r.record_id);
    CHECK(r2.state_id == r.state_id);
    CHECK(r2.content_digest == r.content_digest);
    CHECK(r2.locations.size() == r.locations.size());
    CHECK(r2.locations[1].device_id == DeviceId(3));
    CHECK(r2.compatibility.has_value());
    CHECK(r2.compatibility->outcome == CompatibilityOutcome::COMPATIBLE);
    CHECK(r2.dependencies.size() == 1);
    CHECK(r2.dependencies[0].target_state == StateId(10));
    CHECK(rd.remaining() == 0);
}

static void compatibility_and_dep_semantics() {
    CompatibilityRef exact{CompatibilityId(1), CompatibilityGeneration(1),
                           CompatibilityOutcome::EXACT, Freshness::CURRENT, Evidence::MEASURED, ""};
    CompatibilityRef compat{CompatibilityId(2), CompatibilityGeneration(1),
                            CompatibilityOutcome::COMPATIBLE, Freshness::CURRENT, Evidence::MEASURED, ""};
    CompatibilityRef adapt{CompatibilityId(3), CompatibilityGeneration(1),
                           CompatibilityOutcome::COMPATIBLE_WITH_ADAPTATION, Freshness::CURRENT,
                           Evidence::MEASURED, ""};
    CompatibilityRef none{CompatibilityId(4), CompatibilityGeneration(1),
                          CompatibilityOutcome::UNKNOWN, Freshness::UNKNOWN, Evidence::UNKNOWN, ""};
    CHECK(exact.satisfies(CompatibilityRequirement::EXACT));
    CHECK(compat.satisfies(CompatibilityRequirement::COMPATIBLE));
    CHECK(!compat.satisfies(CompatibilityRequirement::EXACT));
    CHECK(adapt.satisfies(CompatibilityRequirement::COMPATIBLE_WITH_ADAPTATION));
    CHECK(none.satisfies(CompatibilityRequirement::NONE));
    CHECK(!none.satisfies(CompatibilityRequirement::COMPATIBLE));

    TombstoneRecord t;
    t.tombstone_id = TombstoneId(1);
    t.state_id = StateId(5);
    t.floor_generation = StateGeneration(8);
    t.current = true;
    CHECK(t.covers(StateGeneration(8)));
    CHECK(t.covers(StateGeneration(3)));
    CHECK(!t.covers(StateGeneration(9)));
}

int main() {
    int f = 0;
    f += sittest::run("lifecycle_guards", lifecycle_guards);
    f += sittest::run("record_basic", record_basic);
    f += sittest::run("record_serialize_roundtrip", record_serialize_roundtrip);
    f += sittest::run("compatibility_and_dep_semantics", compatibility_and_dep_semantics);
    if (f == 0) std::cout << "ALL PASS\n";
    return f == 0 ? 0 : 1;
}
