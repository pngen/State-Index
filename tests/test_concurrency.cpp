// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "test_util.hpp"
#include "scenarios.hpp"

#include <thread>
#include <vector>

using namespace stateindex;
using namespace siscenario;

static void concurrency_mixed_workload() {
    StateIndexEngine eng(CoordinatorEpoch(1));
    WorkerBootId boot(42);
    eng.register_worker(WorkerId(1), boot);

    const int kThreads = 4;
    const int kIter = 200;
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < kIter; ++i) {
                const StateId sid((t * 1000) + i + 1);
                RecordOpts o;
                o.locations = {{MemoryDomain::HOST_MEMORY, NodeId(1), DeviceId()}};
                auto r = make_record(sid, StateGeneration(1), o);
                eng.register_state(r, env(CoordinatorEpoch(1), boot, StateGeneration(1)));
            }
            // Concurrent exact + predicate queries.
            for (int i = 0; i < kIter; ++i) {
                const StateId sid((t * 1000) + i + 1);
                QueryDescriptor q;
                q.query_id = QueryId(i + 1);
                q.state_id = sid;
                QueryResult res = eng.query(q);
                (void)res;
                QueryDescriptor kq;
                kq.query_id = QueryId(kIter + i + 1);
                kq.kind = StateKind::KV_STATE;
                QueryResult kres = eng.query(kq);
                (void)kres;
            }
        });
    }
    for (auto& th : threads) th.join();

    // After all work, indexes and current authority must be consistent.
    CHECK(eng.invariant_check().empty());
    QueryDescriptor all;
    all.query_id = QueryId(1);
    all.kind = StateKind::KV_STATE;
    QueryResult allRes = eng.query(all);
    CHECK(allRes.ranked_candidates.size() == static_cast<std::size_t>(kThreads * kIter));
    // Accounting must stay coherent (never negative).
    CHECK(eng.accounting().logical_states == static_cast<std::uint64_t>(kThreads * kIter));
}

static void concurrency_same_state_competition() {
    StateIndexEngine eng(CoordinatorEpoch(1));
    WorkerBootId bootA(1), bootB(2);
    eng.register_worker(WorkerId(1), bootA);
    eng.register_worker(WorkerId(2), bootB);
    const StateId sid(9999);

    auto publish = [&](const WorkerBootId& b, int gen) {
        RecordOpts o;
        o.locations = {{MemoryDomain::HOST_MEMORY, NodeId(1), DeviceId()}};
        auto r = make_record(sid, StateGeneration(gen), o);
        return eng.register_state(r, env(CoordinatorEpoch(1), b, StateGeneration(gen)));
    };

    // Two live incarnations compete for the same state; the higher generation
    // wins, lower/duplicate is rejected. No torn state is produced.
    auto v1 = publish(bootA, 1);
    auto v2 = publish(bootB, 1);   // duplicate generation across a competitor
    CHECK(v1.verdict == MutationVerdict::ACCEPTED);
    CHECK(v2.verdict == MutationVerdict::REJECTED_DUPLICATE);
    auto v3 = publish(bootA, 2);
    CHECK(v3.verdict == MutationVerdict::ACCEPTED);  // higher generation wins
    auto v4 = publish(bootB, 2);
    CHECK(v4.verdict == MutationVerdict::REJECTED_DUPLICATE);

    QueryDescriptor q;
    q.query_id = QueryId(1);
    q.state_id = sid;
    QueryResult r = eng.query(q);
    CHECK(r.selected_generation == StateGeneration(2));
    CHECK(eng.invariant_check().empty());
}

int main() {
    int f = 0;
    f += sittest::run("concurrency_mixed_workload", concurrency_mixed_workload);
    f += sittest::run("concurrency_same_state_competition", concurrency_same_state_competition);
    if (f == 0) std::cout << "ALL PASS\n";
    return f == 0 ? 0 : 1;
}
