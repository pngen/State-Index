// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "test_util.hpp"
#include "scenarios.hpp"

using namespace stateindex;
using namespace siscenario;

static void adversarial_generations_and_duplicates() {
    StateIndexEngine eng(CoordinatorEpoch(1));
    WorkerBootId boot(1);
    eng.register_worker(WorkerId(1), boot);
    RecordOpts o;
    o.locations = {{MemoryDomain::HOST_MEMORY, NodeId(1), DeviceId()}};

    // Zero generation rejected.
    {
        auto r = make_record(StateId(1), StateGeneration(), o);
        auto v = eng.register_state(r, env(CoordinatorEpoch(1), boot, StateGeneration()));
        CHECK(v.verdict != MutationVerdict::ACCEPTED);
    }
    // First valid publication.
    {
        auto r = make_record(StateId(1), StateGeneration(1), o);
        CHECK(eng.register_state(r, env(CoordinatorEpoch(1), boot, StateGeneration(1))).verdict == MutationVerdict::ACCEPTED);
    }
    // Generation regression rejected.
    {
        auto r = make_record(StateId(1), StateGeneration(1), o);
        r.record_id = r.record_id.next();
        auto v = eng.register_state(r, env(CoordinatorEpoch(1), boot, StateGeneration(1)));
        CHECK(v.verdict == MutationVerdict::REJECTED_DUPLICATE);
    }
    // Duplicate record id rejected.
    {
        auto r = make_record(StateId(2), StateGeneration(1), o);
        CHECK(eng.register_state(r, env(CoordinatorEpoch(1), boot, StateGeneration(1))).verdict == MutationVerdict::ACCEPTED);
        CHECK(eng.register_state(r, env(CoordinatorEpoch(1), boot, StateGeneration(1))).verdict == MutationVerdict::REJECTED_DUPLICATE);
    }
    // Stale epoch rejected.
    {
        auto r = make_record(StateId(3), StateGeneration(1), o);
        auto v = eng.register_state(r, env(CoordinatorEpoch(5), boot, StateGeneration(1)));
        CHECK(v.verdict == MutationVerdict::REJECTED_STALE_EPOCH);
    }
    // Stale boot rejected.
    {
        auto r = make_record(StateId(4), StateGeneration(1), o);
        auto v = eng.register_state(r, env(CoordinatorEpoch(1), WorkerBootId(999), StateGeneration(1)));
        CHECK(v.verdict == MutationVerdict::REJECTED_STALE_BOOT);
    }
    CHECK(eng.invariant_check().empty());
}

static void adversarial_malformed_location_and_no_usable() {
    StateIndexEngine eng(CoordinatorEpoch(1));
    WorkerBootId boot(2);
    eng.register_worker(WorkerId(2), boot);

    // Current record with no usable location must not be queryable as current.
    RecordOpts bad;
    bad.health = Health::UNHEALTHY;
    bad.locations = {{MemoryDomain::HOST_MEMORY, NodeId(1), DeviceId(), Health::UNHEALTHY}};
    auto r = make_record(StateId(10), StateGeneration(1), bad);
    CHECK(eng.register_state(r, env(CoordinatorEpoch(1), boot, StateGeneration(1))).verdict == MutationVerdict::ACCEPTED);
    QueryDescriptor q;
    q.query_id = QueryId(1);
    q.state_id = StateId(10);
    QueryResult res = eng.query(q);
    /*** a record without a usable location is not returned as a current candidate. ***/
    CHECK(res.ranked_candidates.empty());
    CHECK(res.outcome == QueryOutcome::NOT_FOUND ||
          res.outcome == QueryOutcome::INSUFFICIENT_EVIDENCE || res.outcome == QueryOutcome::STALE_ONLY);
}

static void adversarial_unknown_compat_and_stale_only() {
    StateIndexEngine eng(CoordinatorEpoch(1));
    WorkerBootId boot(3);
    eng.register_worker(WorkerId(3), boot);

    RecordOpts unknown;
    unknown.locations = {{MemoryDomain::HOST_MEMORY, NodeId(1), DeviceId()}};
    unknown.compat = CompatibilityRef{CompatibilityId(1), CompatibilityGeneration(1),
                                      CompatibilityOutcome::UNKNOWN, Freshness::UNKNOWN, Evidence::UNKNOWN, ""};
    auto r = make_record(StateId(20), StateGeneration(1), unknown);
    eng.register_state(r, env(CoordinatorEpoch(1), boot, StateGeneration(1)));

    // UNKNOWN compatibility must not produce FOUND_COMPATIBLE.
    QueryDescriptor q;
    q.query_id = QueryId(2);
    q.state_id = StateId(20);
    q.compatibility = CompatibilityRequirement::COMPATIBLE;
    QueryResult res = eng.query(q);
    CHECK(res.outcome == QueryOutcome::INSUFFICIENT_EVIDENCE);
    CHECK(!res.found());

    // Supersede and then a stale-only (not current) query.
    RecordOpts good = unknown;
    good.compat = CompatibilityRef{CompatibilityId(2), CompatibilityGeneration(1),
                                   CompatibilityOutcome::EXACT, Freshness::CURRENT, Evidence::MEASURED, ""};
    eng.register_state(make_record(StateId(21), StateGeneration(1), good),
                       env(CoordinatorEpoch(1), boot, StateGeneration(1)));
    eng.register_state(make_record(StateId(21), StateGeneration(2), good),
                       env(CoordinatorEpoch(1), boot, StateGeneration(2)));
    QueryDescriptor sq;
    sq.query_id = QueryId(3);
    sq.state_id = StateId(21);
    sq.generation_rule = GenerationRule::EXACT;
    sq.exact_generation = StateGeneration(1);  // historical, non-current
    QueryResult stale = eng.query(sq);
    CHECK(!stale.found());  // a non-current exact generation is never returned as current
    // The current generation, when requested, is generation 2.
    QueryDescriptor cq;
    cq.query_id = QueryId(4);
    cq.state_id = StateId(21);
    QueryResult cur = eng.query(cq);
    CHECK(cur.selected_generation == StateGeneration(2));
}

int main() {
    int f = 0;
    f += sittest::run("adversarial_generations_and_duplicates", adversarial_generations_and_duplicates);
    f += sittest::run("adversarial_malformed_location_and_no_usable", adversarial_malformed_location_and_no_usable);
    f += sittest::run("adversarial_unknown_compat_and_stale_only", adversarial_unknown_compat_and_stale_only);
    if (f == 0) std::cout << "ALL PASS\n";
    return f == 0 ? 0 : 1;
}
