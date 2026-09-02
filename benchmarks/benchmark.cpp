// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
// Completed-work benchmarks. Reports ops/s, ns/op, record count, candidate
// count, thread count, and wall time for real semantic operations.
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "stateindex/stateindex.hpp"
#include "stateindex/protocol.hpp"

using namespace stateindex;

namespace {
std::uint64_t boot = 7000;

MutationEnvelope env(const CoordinatorEpoch& e, const StateGeneration& g = StateGeneration()) {
    MutationEnvelope m; m.coordinator_epoch = e; m.worker_boot = WorkerBootId(boot);
    m.attempt_generation = AttemptGeneration(1); m.dispatch_generation = DispatchGeneration(1);
    m.state_generation = g; return m;
}
StateRecord rec(const StateId& st, const StateGeneration& gen) {
    StateRecord r;
    r.record_id = StateRecordId((st.value() << 32) | gen.value());
    r.state_id = st; r.state_generation = gen; r.record_generation = RecordGeneration(gen.value());
    r.kind = StateKind::KV_STATE; r.namespace_id = NamespaceId(1); r.owner_id = OwnerId(1); r.producer_id = ProducerId(1);
    r.logical_size = 1024; r.lifecycle = Lifecycle::AVAILABLE; r.freshness = Freshness::CURRENT; r.health = Health::HEALTHY;
    r.policy_generation = PolicyGeneration(1); r.created_at = 1000; r.updated_at = 1000; r.description = "bench";
    StateLocation l;
    l.placement_id = PlacementId((st.value() << 24) | (gen.value() << 8) | 1);
    l.replica_id = ReplicaId(l.placement_id.value()); l.node_id = NodeId(1);
    l.domain = MemoryDomain::HOST_MEMORY; l.backend = StorageBackend::MEMORY; l.tier = StorageTier::HOT;
    l.location_generation = LocationGeneration(gen.value()); l.placement_generation = PlacementGeneration(gen.value()); l.replica_generation = ReplicaGeneration(gen.value());
    l.address = "b://" + std::to_string(l.placement_id.value()); l.byte_size = 1024; l.access_class = AccessClass::LOCAL;
    l.health = Health::HEALTHY; l.freshness = Freshness::CURRENT; l.provenance = Evidence::MEASURED;
    l.retrieval.evidence = Evidence::MEASURED; l.authority_epoch = CoordinatorEpoch(1); l.authority_boot = WorkerBootId(boot);
    r.locations.push_back(l);
    r.semantic_digest = r.compute_semantic_digest();
    return r;
}
std::string ms(const char* name, std::uint64_t count, std::uint64_t candidates, double wall_s) {
    const double ops = static_cast<double>(count) / wall_s;
    const double ns = wall_s * 1e9 / static_cast<double>(count);
    return std::string(name) + " records=" + std::to_string(count) + " candidates=" + std::to_string(candidates) +
           " threads=1 ops/s=" + std::to_string(static_cast<std::uint64_t>(ops)) +
           " ns/op=" + std::to_string(static_cast<std::uint64_t>(ns)) +
           " wall=" + std::to_string(wall_s) + "s";
}
}  // namespace

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    const std::size_t sizes[] = {1000, 10000, 100000};
    for (const std::size_t n : sizes) {
        StateIndexEngine eng(CoordinatorEpoch(1));
        eng.register_worker(WorkerId(1), WorkerBootId(boot));
        MutationEnvelope e = env(CoordinatorEpoch(1), StateGeneration(1));

        // Register n states.
        const auto t0 = std::chrono::steady_clock::now();
        for (std::uint64_t i = 1; i <= n; ++i) eng.register_state(rec(StateId(i), StateGeneration(1)), e);
        const auto t1 = std::chrono::steady_clock::now();
        const double reg_s = std::chrono::duration<double>(t1 - t0).count();
        std::cout << ms("register+update", n, 1, reg_s) << "\n";

        // Exact lookup (n operations).
        const auto t2 = std::chrono::steady_clock::now();
        QueryResult last;
        for (std::uint64_t i = 1; i <= n; ++i) {
            QueryDescriptor q; q.query_id = QueryId(i); q.state_id = StateId(i);
            last = eng.query(q);
        }
        const auto t3 = std::chrono::steady_clock::now();
        std::cout << ms("exact-lookup", n, last.ranked_candidates.size(), std::chrono::duration<double>(t3 - t2).count()) << "\n";

        // Kind lookup: a single query's candidate set is n; run a bounded number
        // of completed queries so the metric reflects real per-query cost and
        // the reported candidate count is the population of the kind index.
        const std::uint64_t kq = 20;
        const auto t4 = std::chrono::steady_clock::now();
        for (std::uint64_t i = 1; i <= kq; ++i) {
            QueryDescriptor q; q.query_id = QueryId(i); q.kind = StateKind::KV_STATE;
            (void)eng.query(q);
        }
        const auto t5 = std::chrono::steady_clock::now();
        std::cout << ms("kind-lookup", kq, n, std::chrono::duration<double>(t5 - t4).count()) << "\n";

        // Secondary-index rebuild.
        const auto t6 = std::chrono::steady_clock::now();
        SecondaryIndexes idx;
        idx.rebuild(eng.canonical_records());
        const auto t7 = std::chrono::steady_clock::now();
        std::cout << ms("index-rebuild", n, n, std::chrono::duration<double>(t7 - t6).count()) << "\n";

        // Persistence serialize + recover.
        const auto t8 = std::chrono::steady_clock::now();
        eng.save("bench_persist.bin");
        StateIndexEngine re(CoordinatorEpoch(1));
        re.load("bench_persist.bin");
        const auto t9 = std::chrono::steady_clock::now();
        std::cout << ms("persist-serialize+recover", n, n, std::chrono::duration<double>(t9 - t8).count()) << "\n";

        // Protocol encode/decode.
        std::vector<std::uint8_t> payload;
        {
            ByteWriter w; QueryDescriptor q; q.query_id = QueryId(1); q.state_id = StateId(1); q.serialize(w); payload = w.data();
        }
        const auto t10 = std::chrono::steady_clock::now();
        for (std::uint64_t i = 0; i < n; ++i) {
            auto fr = encode_frame(MsgKind::QUERY, payload);
            auto [f, err] = decode_frame(ByteSpan(fr));
            (void)f; (void)err;
        }
        const auto t11 = std::chrono::steady_clock::now();
        std::cout << ms("protocol-encode/decode", n, 0, std::chrono::duration<double>(t11 - t10).count()) << "\n";
        std::cout.flush();
    }
    std::remove("bench_persist.bin");
    return 0;
}
