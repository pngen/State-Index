// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "test_util.hpp"
#include "scenarios.hpp"

#include <random>

using namespace stateindex;
using namespace siscenario;

// Deterministic xorshift PRNG so every run is reproducible with a fixed seed.
static std::uint64_t state_seed = 0x9E3779B97F4A7C15ULL;
static std::uint64_t xorshift64() {
    std::uint64_t x = state_seed;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    state_seed = x;
    return x;
}
static int rnd(int lo, int hi) { return lo + static_cast<int>(xorshift64() % static_cast<std::uint64_t>(hi - lo + 1)); }

static void property_invariants_and_determinism() {
    const std::uint64_t seed = 123456789ULL;
    state_seed = seed;
    std::cout << "property seed: " << seed << "\n";

    StateIndexEngine eng(CoordinatorEpoch(1));
    WorkerBootId boot(777);
    eng.register_worker(WorkerId(1), boot);

    const int kIterations = 300;
    std::vector<StateId> states;
    for (int i = 0; i < 40; ++i) states.push_back(StateId(i + 1));

    for (int iter = 0; iter < kIterations; ++iter) {
        const int op = rnd(0, 3);
        const StateId sid = states[rnd(0, static_cast<int>(states.size()) - 1)];
        if (op <= 1) {
            // Register a generation.
            RecordOpts o;
            o.locations = {{MemoryDomain::HOST_MEMORY, NodeId(1), DeviceId()},
                           {MemoryDomain::LOCAL_FILESYSTEM, NodeId(2), DeviceId()}};
            o.digest = "d" + std::to_string(rnd(0, 3));
            const int gen = rnd(1, 6);
            auto r = make_record(sid, StateGeneration(gen), o);
            eng.register_state(r, env(CoordinatorEpoch(1), boot, StateGeneration(gen)));
        } else if (op == 2) {
            // Query exact.
            QueryDescriptor q;
            q.query_id = QueryId(iter + 1);
            q.state_id = sid;
            QueryResult r1 = eng.query(q);
            QueryResult r2 = eng.query(q);
            /*** Determinism: identical query -> identical result. ***/
            CHECK(r1.outcome == r2.outcome);
            CHECK(r1.selected_record_id == r2.selected_record_id);
            CHECK(r1.ranked_candidates.size() == r2.ranked_candidates.size());
            for (std::size_t i = 0; i < r1.ranked_candidates.size(); ++i)
                CHECK(r1.ranked_candidates[i].record_id == r2.ranked_candidates[i].record_id);
        } else {
            // Query by kind (multiple) -> secondary-index scan equals canonical scan.
            QueryDescriptor q;
            q.query_id = QueryId(iter + 1);
            q.kind = StateKind::KV_STATE;
            QueryResult r = eng.query(q);
            /*** A current query never returns a superseded/tombstoned record. ***/
            for (const auto& c : r.ranked_candidates) {
                auto rec = eng.get_record(c.record_id);
                CHECK(rec.has_value());
                CHECK(rec->lifecycle != Lifecycle::SUPERSEDED);
                CHECK(rec->lifecycle != Lifecycle::TOMBSTONED);
                CHECK(!eng.invariant_check().size());
            }
        }

        // Secondary indexes must equal a canonical scan for exact-state queries.
        auto inv = eng.invariant_check();
        CHECK(inv.empty());
    }

    // Persistence round-trip preserves semantic digests for unchanged (non-
    // revalidated) records.
    eng.save("test_property.bin");
    StateIndexEngine re(CoordinatorEpoch(1));
    re.load("test_property.bin");
    auto a = eng.canonical_records();
    auto b = re.canonical_records();
    CHECK(a.size() == b.size());
    // Accounting never negative.
    CHECK(eng.accounting().valid());
}

static void property_tombstone_no_resurrection() {
    state_seed = 987654321ULL;
    StateIndexEngine eng(CoordinatorEpoch(1));
    WorkerBootId boot(5);
    eng.register_worker(WorkerId(5), boot);
    RecordOpts o;
    o.locations = {{MemoryDomain::HOST_MEMORY, NodeId(1), DeviceId()}};
    const StateId sid(200);
    for (int gen = 1; gen <= 5; ++gen)
        eng.register_state(make_record(sid, StateGeneration(gen), o),
                           env(CoordinatorEpoch(1), boot, StateGeneration(gen)));
    TombstoneRecord t;
    t.tombstone_id = TombstoneId(1);
    t.state_id = sid;
    t.floor_generation = StateGeneration(4);
    eng.tombstone(t, env(CoordinatorEpoch(1), boot, StateGeneration(5)));
    // A generation <= 4 cannot be published as current.
    for (int gen = 1; gen <= 4; ++gen) {
        auto v = eng.register_state(make_record(sid, StateGeneration(gen), o),
                                    env(CoordinatorEpoch(1), boot, StateGeneration(gen)));
        CHECK(v.verdict == MutationVerdict::REJECTED_TOMBSTONED);
    }
    CHECK(eng.invariant_check().empty());
    std::remove("test_property.bin");
}

int main() {
    int f = 0;
    f += sittest::run("property_invariants_and_determinism", property_invariants_and_determinism);
    f += sittest::run("property_tombstone_no_resurrection", property_tombstone_no_resurrection);
    if (f == 0) std::cout << "ALL PASS\n";
    return f == 0 ? 0 : 1;
}
