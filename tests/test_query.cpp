// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "test_util.hpp"
#include "scenarios.hpp"

using namespace stateindex;
using namespace siscenario;

static std::uint64_t record_counter = 0;

// Helper: register a state with a fresh worker boot against the engine.
static void reg(StateIndexEngine& eng, const WorkerBootId& boot, const CoordinatorEpoch& epoch,
                const StateId& state, const StateGeneration& gen, RecordOpts opts) {
    eng.register_worker(WorkerId(1), boot);
    auto r = make_record(state, gen, opts);
    auto v = eng.register_state(r, env(epoch, boot, gen));
    CHECK(v.verdict == MutationVerdict::ACCEPTED);
}

static void exact_identity_lookup() {
    StateIndexEngine eng(CoordinatorEpoch(1));
    WorkerBootId boot(1);
    RecordOpts o;
    o.locations = {{MemoryDomain::HOST_MEMORY, NodeId(1), DeviceId()}};
    reg(eng, boot, CoordinatorEpoch(1), StateId(5), StateGeneration(1), o);

    QueryDescriptor q;
    q.query_id = QueryId(1);
    q.state_id = StateId(5);
    q.kind = StateKind::KV_STATE;
    QueryResult r = eng.query(q);
    CHECK(r.outcome == QueryOutcome::FOUND_EXACT);
    CHECK(r.selected_generation == StateGeneration(1));
    CHECK(r.ranked_candidates.size() == 1);
    CHECK(r.eliminated.empty());
    CHECK(eng.invariant_check().empty());
}

static void kind_lookup_multiple() {
    StateIndexEngine eng(CoordinatorEpoch(1));
    WorkerBootId boot(1);
    RecordOpts o;
    o.locations = {{MemoryDomain::HOST_MEMORY, NodeId(1), DeviceId()}};
    reg(eng, boot, CoordinatorEpoch(1), StateId(10), StateGeneration(1), o);
    reg(eng, boot, CoordinatorEpoch(1), StateId(11), StateGeneration(1), o);
    reg(eng, boot, CoordinatorEpoch(1), StateId(12), StateGeneration(1), o);

    QueryDescriptor q;
    q.query_id = QueryId(2);
    q.kind = StateKind::KV_STATE;
    QueryResult r = eng.query(q);
    CHECK(r.outcome == QueryOutcome::FOUND_MULTIPLE);
    CHECK(r.ranked_candidates.size() == 3);
    // Deterministic tie-break: candidates sorted by generation then record id.
}

static void supersession_and_history() {
    StateIndexEngine eng(CoordinatorEpoch(1));
    WorkerBootId boot(1);
    RecordOpts o;
    o.locations = {{MemoryDomain::HOST_MEMORY, NodeId(1), DeviceId()}};
    reg(eng, boot, CoordinatorEpoch(1), StateId(20), StateGeneration(1), o);
    reg(eng, boot, CoordinatorEpoch(1), StateId(20), StateGeneration(2), o);

    QueryDescriptor q;
    q.query_id = QueryId(3);
    q.state_id = StateId(20);
    QueryResult r = eng.query(q);
    CHECK(r.outcome == QueryOutcome::FOUND_EXACT);
    CHECK(r.selected_generation == StateGeneration(2));  // gen 2 is current

    // Historical query returns both generations, labelled non-current.
    QueryDescriptor hq;
    hq.query_id = QueryId(4);
    hq.state_id = StateId(20);
    hq.historical_only = true;
    QueryResult hr = eng.query_historical(hq);
    CHECK(hr.ranked_candidates.size() == 2);
    CHECK(!hr.notes.empty());
    // Gen 1 record is superseded and not eligible for current ranking.
    auto hist = eng.history_of(StateId(20));
    CHECK(hist.size() == 2);
    CHECK(hist[0].state_generation == StateGeneration(2));
    CHECK(hist[1].lifecycle == Lifecycle::SUPERSEDED);
}

static void compatibility_filter() {
    StateIndexEngine eng(CoordinatorEpoch(1));
    WorkerBootId boot(1);
    RecordOpts exact;
    exact.locations = {{MemoryDomain::HOST_MEMORY, NodeId(1), DeviceId()}};
    exact.compat = CompatibilityRef{CompatibilityId(1), CompatibilityGeneration(1),
                                    CompatibilityOutcome::EXACT, Freshness::CURRENT, Evidence::MEASURED, ""};
    reg(eng, boot, CoordinatorEpoch(1), StateId(30), StateGeneration(1), exact);

    RecordOpts compat;
    compat.locations = exact.locations;
    compat.compat = CompatibilityRef{CompatibilityId(2), CompatibilityGeneration(1),
                                     CompatibilityOutcome::COMPATIBLE, Freshness::CURRENT, Evidence::MEASURED, ""};
    reg(eng, boot, CoordinatorEpoch(1), StateId(31), StateGeneration(1), compat);

    RecordOpts unknown;
    unknown.locations = exact.locations;
    unknown.compat = CompatibilityRef{CompatibilityId(3), CompatibilityGeneration(1),
                                      CompatibilityOutcome::UNKNOWN, Freshness::UNKNOWN, Evidence::UNKNOWN, ""};
    reg(eng, boot, CoordinatorEpoch(1), StateId(32), StateGeneration(1), unknown);

    // Query requiring EXACT must only accept exact.
    QueryDescriptor q;
    q.query_id = QueryId(5);
    q.kind = StateKind::KV_STATE;
    q.compatibility = CompatibilityRequirement::EXACT;
    QueryResult r = eng.query(q);
    CHECK(r.ranked_candidates.size() == 1);
    CHECK(*r.selected_record_id == record_id_of(StateId(30), StateGeneration(1)));
    CHECK(r.outcome == QueryOutcome::FOUND_COMPATIBLE);  // kind-only query, not exact identity
    CHECK(r.found());

    // Query requiring COMPATIBLE accepts exact + compatible.
    q.compatibility = CompatibilityRequirement::COMPATIBLE;
    QueryResult r2 = eng.query(q);
    CHECK(r2.ranked_candidates.size() == 2);

    // Query requiring COMPATIBLE must NOT include the UNKNOWN-compat record.
    for (auto& c : r2.ranked_candidates) CHECK(c.record_id != record_id_of(StateId(32), StateGeneration(1)));

    // UNKNOWN compatibility never becomes FOUND_COMPATIBLE.
    QueryDescriptor qa;
    qa.query_id = QueryId(6);
    qa.state_id = StateId(32);
    qa.compatibility = CompatibilityRequirement::COMPATIBLE;
    QueryResult ra = eng.query(qa);
    CHECK(ra.outcome == QueryOutcome::INSUFFICIENT_EVIDENCE);  // UNKNOWN compatibility never becomes compatible
    CHECK(!ra.found());
}

static void dependency_filter_and_invalidation() {
    StateIndexEngine eng(CoordinatorEpoch(1));
    WorkerBootId boot(1);
    RecordOpts dep;
    dep.locations = {{MemoryDomain::HOST_MEMORY, NodeId(1), DeviceId()}};
    reg(eng, boot, CoordinatorEpoch(1), StateId(40), StateGeneration(1), dep);

    RecordOpts consumer;
    consumer.locations = dep.locations;
    consumer.deps = {DependencyRef{DependencyId(1), DependencyGeneration(1), DependencyKind::STATE,
                                   StateId(40), StateGeneration(1), true}};
    reg(eng, boot, CoordinatorEpoch(1), StateId(41), StateGeneration(1), consumer);

    // Consumer depends on state 40; query the consumer with dependency requirement.
    QueryDescriptor q;
    q.query_id = QueryId(7);
    q.state_id = StateId(41);
    q.dependency_requirements.push_back(DependencyRequirement{StateId(40), StateGeneration(1), true});
    QueryResult r = eng.query(q);
    CHECK(r.found());

    // Invalidate the dependency => consumer becomes ineligible.
    InvalidationRecord inv;
    inv.invalidation_id = InvalidationId(1);
    inv.state_id = StateId(40);
    inv.bound_generation = StateGeneration(1);
    auto vr = eng.invalidate(inv, env(CoordinatorEpoch(1), boot, StateGeneration(1)));
    CHECK(vr.verdict == MutationVerdict::ACCEPTED);

    QueryResult r2 = eng.query(q);
    CHECK(r2.outcome == QueryOutcome::INSUFFICIENT_EVIDENCE ||
          r2.outcome == QueryOutcome::STALE_ONLY ||
          r2.outcome == QueryOutcome::INCOMPATIBLE_ONLY ||
          r2.outcome == QueryOutcome::INVALIDATED_ONLY);
    CHECK(!r2.found());
}

static void locality_ranking() {
    StateIndexEngine eng(CoordinatorEpoch(1));
    WorkerBootId boot(1);
    RecordOpts exact_far;
    exact_far.locations = {{MemoryDomain::OBJECT_STORAGE_CLASS, NodeId(2), DeviceId(), Health::HEALTHY,
                            Freshness::CURRENT, Evidence::SYNTHETIC, 50.0, 100.0, 1024.0}};
    exact_far.compat = CompatibilityRef{CompatibilityId(1), CompatibilityGeneration(1),
                                        CompatibilityOutcome::EXACT, Freshness::CURRENT, Evidence::MEASURED, ""};
    reg(eng, boot, CoordinatorEpoch(1), StateId(50), StateGeneration(1), exact_far);

    RecordOpts local;
    local.locations = {{MemoryDomain::HOST_MEMORY, NodeId(1), DeviceId(), Health::HEALTHY,
                        Freshness::CURRENT, Evidence::MEASURED, 1.0, 10000.0, 1024.0}};
    local.compat = CompatibilityRef{CompatibilityId(2), CompatibilityGeneration(1),
                                    CompatibilityOutcome::COMPATIBLE, Freshness::CURRENT, Evidence::MEASURED, ""};
    reg(eng, boot, CoordinatorEpoch(1), StateId(51), StateGeneration(1), local);

    // Query preferring local locality.
    QueryDescriptor q;
    q.query_id = QueryId(8);
    q.kind = StateKind::KV_STATE;
    q.locality_preference = {MemoryDomain::HOST_MEMORY};
    QueryResult r = eng.query(q);
    CHECK(r.outcome == QueryOutcome::FOUND_MULTIPLE);
    CHECK(*r.selected_record_id == record_id_of(StateId(51), StateGeneration(1)));  // local wins
}

int main() {
    int f = 0;
    f += sittest::run("exact_identity_lookup", exact_identity_lookup);
    f += sittest::run("kind_lookup_multiple", kind_lookup_multiple);
    f += sittest::run("supersession_and_history", supersession_and_history);
    f += sittest::run("compatibility_filter", compatibility_filter);
    f += sittest::run("dependency_filter_and_invalidation", dependency_filter_and_invalidation);
    f += sittest::run("locality_ranking", locality_ranking);
    if (f == 0) std::cout << "ALL PASS\n";
    return f == 0 ? 0 : 1;
}
