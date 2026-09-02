// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "test_util.hpp"
#include "scenarios.hpp"

using namespace stateindex;
using namespace siscenario;

static void index_roundtrip_and_verify() {
    SecondaryIndexes idx;
    std::vector<StateRecord> recs;

    RecordOpts oa;
    oa.locations = {{MemoryDomain::HOST_MEMORY, NodeId(1), DeviceId()}};
    oa.digest = "digestA";
    oa.compat = CompatibilityRef{CompatibilityId(1), CompatibilityGeneration(1),
                                 CompatibilityOutcome::EXACT, Freshness::CURRENT, Evidence::MEASURED, ""};
    oa.deps = {DependencyRef{DependencyId(1), DependencyGeneration(1), DependencyKind::STATE, StateId(100), StateGeneration(1), true}};
    recs.push_back(make_record(StateId(5), StateGeneration(1), oa));

    RecordOpts ob = oa;
    ob.kind = StateKind::TENSOR_STATE;
    ob.ns = NamespaceId(2);
    ob.digest = "digestB";
    recs.push_back(make_record(StateId(6), StateGeneration(1), ob));

    RecordOpts oc = oa;
    recs.push_back(make_record(StateId(5), StateGeneration(2), oc));  // second generation

    idx.rebuild(recs);
    CHECK(idx.by_state(StateId(5)).size() == 2);          // both generations
    CHECK(idx.by_kind(StateKind::KV_STATE).size() == 2);
    CHECK(idx.by_digest("digestA").size() == 2);          // gen1 and gen2 both have digestA
    CHECK(idx.by_digest("digestB").size() == 1);
    CHECK(idx.by_compat(CompatibilityId(1)).size() == 3);  // state5 gen1, state6, state5 gen2
    CHECK(idx.by_dep_target(StateId(100)).size() == 3);
    CHECK(idx.by_node(NodeId(1)).size() == 3);
    CHECK(idx.by_domain(MemoryDomain::HOST_MEMORY).size() == 3);
    CHECK(idx.by_generation(StateGeneration(1)).size() > 0);

    auto errors = idx.verify(recs);
    CHECK_MSG(errors.empty(), errors.empty() ? "" : errors[0]);

    // Remove the generation-2 record for state 5 and re-verify against the
    // canonical set now excluding it.
    idx.remove(recs[2]);
    CHECK(idx.by_state(StateId(5)).size() == 1);
    CHECK(idx.by_compat(CompatibilityId(1)).size() == 2);
    CHECK(idx.by_dep_target(StateId(100)).size() == 2);
    std::vector<StateRecord> recs2{recs[0], recs[1]};
    auto errors2 = idx.verify(recs2);
    CHECK_MSG(errors2.empty(), errors2.empty() ? "" : errors2[0]);
}

static void index_location_owner() {
    SecondaryIndexes idx;
    RecordOpts o;
    o.locations = {
        {MemoryDomain::CUDA_DEVICE, NodeId(1), DeviceId(2)},
        {MemoryDomain::LOCAL_FILESYSTEM, NodeId(1), DeviceId()},
    };
    StateRecord r = make_record(StateId(77), StateGeneration(1), o);
    std::vector<StateRecord> recs{r};
    idx.rebuild(recs);
    PlacementId p = r.locations[0].placement_id;
    auto owner = idx.location_owner(p);
    CHECK(owner.has_value());
    CHECK(*owner == r.record_id);
    CHECK(idx.by_device(DeviceId(2)).size() == 1);
    CHECK(idx.by_domain(MemoryDomain::LOCAL_FILESYSTEM).size() == 1);
}

int main() {
    int f = 0;
    f += sittest::run("index_roundtrip_and_verify", index_roundtrip_and_verify);
    f += sittest::run("index_location_owner", index_location_owner);
    if (f == 0) std::cout << "ALL PASS\n";
    return f == 0 ? 0 : 1;
}
