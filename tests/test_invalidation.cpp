// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "test_util.hpp"
#include "scenarios.hpp"

using namespace stateindex;
using namespace siscenario;

static void stale_invalidation_rejected() {
    StateIndexEngine eng(CoordinatorEpoch(1));
    WorkerBootId boot(1);
    RecordOpts o;
    o.locations = {{MemoryDomain::HOST_MEMORY, NodeId(1), DeviceId()}};
    auto r = make_record(StateId(5), StateGeneration(5), o);
    eng.register_worker(WorkerId(1), boot);
    CHECK(eng.register_state(r, env(CoordinatorEpoch(1), boot, StateGeneration(5))).verdict == MutationVerdict::ACCEPTED);

    // An invalidation bound below the current generation must be rejected as stale.
    InvalidationRecord inv;
    inv.invalidation_id = InvalidationId(1);
    inv.state_id = StateId(5);
    inv.bound_generation = StateGeneration(3);
    auto v = eng.invalidate(inv, env(CoordinatorEpoch(1), boot, StateGeneration(1)));
    CHECK(v.verdict == MutationVerdict::REJECTED_NON_MONOTONIC);
}

static void tombstone_prevents_resurrection() {
    StateIndexEngine eng(CoordinatorEpoch(1));
    WorkerBootId boot(9);
    RecordOpts o;
    o.locations = {{MemoryDomain::HOST_MEMORY, NodeId(1), DeviceId()}};
    {
        eng.register_worker(WorkerId(9), boot);
        auto r = make_record(StateId(60), StateGeneration(2), o);
        CHECK(eng.register_state(r, env(CoordinatorEpoch(1), boot, StateGeneration(2))).verdict == MutationVerdict::ACCEPTED);
    }

    TombstoneRecord t;
    t.tombstone_id = TombstoneId(1);
    t.state_id = StateId(60);
    t.floor_generation = StateGeneration(2);
    auto tv = eng.tombstone(t, env(CoordinatorEpoch(1), boot, StateGeneration(2)));
    CHECK(tv.verdict == MutationVerdict::ACCEPTED);

    // A stale producer cannot republish generation <= floor as current.
    {
        auto r = make_record(StateId(60), StateGeneration(2), o);
        auto v = eng.register_state(r, env(CoordinatorEpoch(1), boot, StateGeneration(2)));
        CHECK(v.verdict == MutationVerdict::REJECTED_TOMBSTONED);
    }

    // Even a fresh higher generation below floor is rejected; above floor ok.
    {
        auto r = make_record(StateId(60), StateGeneration(1), o);
        auto v = eng.register_state(r, env(CoordinatorEpoch(1), boot, StateGeneration(1)));
        CHECK(v.verdict == MutationVerdict::REJECTED_TOMBSTONED);
    }

    // Current should no longer be the tombstoned generation.
    QueryDescriptor q;
    q.query_id = QueryId(1);
    q.state_id = StateId(60);
    QueryResult r2 = eng.query(q);
    CHECK(r2.outcome == QueryOutcome::INVALIDATED_ONLY || r2.outcome == QueryOutcome::NOT_FOUND ||
          r2.outcome == QueryOutcome::INSUFFICIENT_EVIDENCE || r2.outcome == QueryOutcome::STALE_ONLY);
    CHECK(!r2.found());
    bool saw_tombstone = false;
    for (const auto& e : r2.eliminated)
        if (e.reason == RejectionReason::TOMBSTONED || e.reason == RejectionReason::INVALIDATED) saw_tombstone = true;
    CHECK(saw_tombstone);
    CHECK(eng.invariant_check().empty());
}

static void duplicate_and_regression_rejected() {
    StateIndexEngine eng(CoordinatorEpoch(1));
    WorkerBootId boot(1);
    RecordOpts o;
    o.locations = {{MemoryDomain::HOST_MEMORY, NodeId(1), DeviceId()}};
    eng.register_worker(WorkerId(1), boot);
    auto r1 = make_record(StateId(70), StateGeneration(1), o);
    CHECK(eng.register_state(r1, env(CoordinatorEpoch(1), boot, StateGeneration(1))).verdict == MutationVerdict::ACCEPTED);
    // Duplicate record id rejected.
    CHECK(eng.register_state(r1, env(CoordinatorEpoch(1), boot, StateGeneration(1))).verdict == MutationVerdict::REJECTED_DUPLICATE);
    // A distinct record id but same (state, generation) must be rejected as a
    // competing duplicate.
    auto r3 = make_record(StateId(70), StateGeneration(1), o);
    r3.record_id = r3.record_id.next();
    auto v = eng.register_state(r3, env(CoordinatorEpoch(1), boot, StateGeneration(1)));
    CHECK(v.verdict == MutationVerdict::REJECTED_DUPLICATE);
}

int main() {
    int f = 0;
    f += sittest::run("stale_invalidation_rejected", stale_invalidation_rejected);
    f += sittest::run("tombstone_prevents_resurrection", tombstone_prevents_resurrection);
    f += sittest::run("duplicate_and_regression_rejected", duplicate_and_regression_rejected);
    if (f == 0) std::cout << "ALL PASS\n";
    return f == 0 ? 0 : 1;
}
