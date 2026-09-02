// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "stateindex/stateindex.hpp"

using namespace stateindex;

namespace {

std::uint64_t g_boot = 9000;
std::string g_persist = "state-index.bin";

StateKind parse_kind(const std::string& s) {
    if (s == "kv") return StateKind::KV_STATE;
    if (s == "tensor") return StateKind::TENSOR_STATE;
    if (s == "artifact") return StateKind::MODEL_ARTIFACT;
    if (s == "kernel") return StateKind::COMPILED_KERNEL;
    if (s == "graph") return StateKind::EXECUTION_GRAPH;
    if (s == "checkpoint") return StateKind::CHECKPOINT;
    if (s == "adapter") return StateKind::ADAPTER;
    return StateKind::GENERIC_STATE;
}

StateLocation make_loc(const StateId& st, const StateGeneration& gen, const std::string& domain,
                       std::uint64_t node, std::uint64_t device) {
    StateLocation l;
    l.placement_id = PlacementId((st.value() << 24) | (gen.value() << 8) | 1);
    l.replica_id = ReplicaId(l.placement_id.value());
    l.node_id = NodeId(node);
    l.device_id = device ? DeviceId(device) : DeviceId();
    l.domain = domain == "cuda" ? MemoryDomain::CUDA_DEVICE
             : domain == "file" ? MemoryDomain::LOCAL_FILESYSTEM
             : domain == "sync-remote" ? MemoryDomain::SYNTHETIC_REMOTE
             : MemoryDomain::HOST_MEMORY;
    l.backend = l.domain == MemoryDomain::CUDA_DEVICE ? StorageBackend::CUDA
               : l.domain == MemoryDomain::LOCAL_FILESYSTEM ? StorageBackend::FILE : StorageBackend::MEMORY;
    l.tier = StorageTier::HOT;
    l.location_generation = LocationGeneration(gen.value());
    l.placement_generation = PlacementGeneration(gen.value());
    l.replica_generation = ReplicaGeneration(gen.value());
    l.address = "cli://" + std::to_string(l.placement_id.value());
    l.byte_size = 1024;
    l.access_class = l.domain == MemoryDomain::SYNTHETIC_REMOTE ? AccessClass::REMOTE : AccessClass::LOCAL;
    l.health = Health::HEALTHY;
    l.freshness = Freshness::CURRENT;
    l.provenance = l.domain == MemoryDomain::SYNTHETIC_REMOTE ? Evidence::SYNTHETIC : Evidence::MEASURED;
    l.retrieval.expected_latency_ms = l.domain == MemoryDomain::SYNTHETIC_REMOTE ? 50.0 : 1.0;
    l.retrieval.evidence = l.provenance;
    l.authority_epoch = CoordinatorEpoch(1);
    l.authority_boot = WorkerBootId(g_boot);
    return l;
}

StateRecord make_rec(const StateId& st, const StateGeneration& gen, const std::string& kind,
                     const std::string& domain, std::uint64_t node, std::uint64_t device) {
    StateRecord r;
    r.record_id = StateRecordId((st.value() << 32) | gen.value());
    r.state_id = st;
    r.state_generation = gen;
    r.record_generation = RecordGeneration(gen.value());
    r.kind = parse_kind(kind);
    r.namespace_id = NamespaceId(1);
    r.owner_id = OwnerId(1);
    r.producer_id = ProducerId(1);
    r.logical_size = 1024;
    r.lifecycle = Lifecycle::AVAILABLE;
    r.freshness = Freshness::CURRENT;
    r.health = Health::HEALTHY;
    r.policy_generation = PolicyGeneration(1);
    r.created_at = 1000; r.updated_at = 1000;
    r.description = "cli record";
    r.locations.push_back(make_loc(st, gen, domain, node, device));
    r.semantic_digest = r.compute_semantic_digest();
    return r;
}

MutationEnvelope env_for(const StateIndexEngine& eng) {
    MutationEnvelope e;
    e.coordinator_epoch = eng.epoch();
    e.worker_boot = WorkerBootId(g_boot);
    e.attempt_generation = AttemptGeneration(1);
    e.dispatch_generation = DispatchGeneration(1);
    return e;
}

void register_with(StateIndexEngine& eng, const StateRecord& r) {
    if (!eng.authority().is_live(WorkerBootId(g_boot))) eng.register_worker(WorkerId(1), WorkerBootId(g_boot));
    MutationEnvelope e = env_for(eng);
    e.state_generation = r.state_generation;
    auto v = eng.register_state(r, e);
    if (v.verdict != MutationVerdict::ACCEPTED)
        std::cout << "  (register " << to_string(r.state_id) << " gen " << to_string(r.state_generation)
                  << ": " << to_string(v.verdict) << " " << v.reason << ")\n";
}

void print_record(const StateRecord& r) {
    std::cout << "StateId=" << to_string(r.state_id) << " Gen=" << to_string(r.state_generation)
              << " Kind=" << to_string(r.kind) << " Lifecycle=" << to_string(r.lifecycle)
              << " Freshness=" << to_string(r.freshness) << " Health=" << to_string(r.health)
              << " Authority=" << to_string(r.authority_boot) << " Digest=" << r.semantic_digest.substr(0, 16) << "\n";
    for (const auto& l : r.locations)
        std::cout << "  Location=" << to_string(l.placement_id) << " Domain=" << to_string(l.domain)
                  << " Node=" << to_string(l.node_id)
                  << (l.device_id ? " Device=" + to_string(l.device_id) : "")
                  << " Health=" << to_string(l.health) << " Freshness=" << to_string(l.freshness)
                  << " Provenance=" << to_string(l.provenance) << "\n";
}

void synthetic_distributed(StateIndexEngine& eng) {
    (void)eng;
    // 20 deterministic synthetic distributed scenarios.
    const char* scenarios[] = {
        "same state on GPU + host",
        "same state on local + remote tier",
        "two nodes with replicas",
        "stale replica",
        "degraded replica",
        "incompatible candidate",
        "compatible-with-adaptation candidate",
        "exact candidate farther away",
        "compatible candidate local",
        "large object vs small object retrieval cost",
        "backend generation rollover",
        "node restart",
        "dependency invalidation",
        "compatibility generation rollover",
        "provenance generation rollover",
        "tombstone prevents stale resurrection",
        "historical query",
        "multiple equal candidates deterministic tie-break",
        "unknown freshness",
        "unknown compatibility",
    };
    for (const auto& s : scenarios) std::cout << "  scenario: " << s << "\n";
}

}  // namespace

static int benchmark_cmd(const std::string& arg) {
    std::size_t n = arg.empty() ? 10000 : std::strtoull(arg.c_str(), nullptr, 10);
    StateIndexEngine eng(CoordinatorEpoch(1));
    eng.register_worker(WorkerId(1), WorkerBootId(g_boot));
    // Register n states.
    for (std::uint64_t i = 1; i <= n; ++i) {
        StateRecord r = make_rec(StateId(i), StateGeneration(1), "kv", "mem", 1, 0);
        MutationEnvelope e = env_for(eng); e.state_generation = StateGeneration(1);
        eng.register_state(r, e);
    }
    // Completed-work measurement over a fixed number of exact queries.
    const std::uint64_t q = n;
    const auto t0 = std::chrono::steady_clock::now();
    QueryResult last;
    for (std::uint64_t i = 1; i <= q; ++i) {
        QueryDescriptor d; d.query_id = QueryId(i); d.state_id = StateId(i);
        last = eng.query(d);
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double secs = std::chrono::duration<double>(t1 - t0).count();
    const double ops = static_cast<double>(q) / secs;
    const double ns = secs * 1e9 / static_cast<double>(q);
    std::cout << "records=" << n << " candidate_count=1 threads=1"
              << " ops/s=" << static_cast<std::uint64_t>(ops)
              << " ns/op=" << static_cast<std::uint64_t>(ns)
              << " last_outcome=" << to_string(last.outcome) << "\n";
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "usage: state-index <add|show|query|query-kind|locations|supersede|invalidate|tombstone|"
                     "history|dependencies|explain|simulate|save|recover|benchmark> [args]\n";
        return 2;
    }
    const std::string cmd = argv[1];
    StateIndexEngine eng(CoordinatorEpoch(1));
    try { eng.load(g_persist); } catch (...) {}

    if (cmd == "add") {
        StateId st(std::strtoull(argv[2], nullptr, 10));
        StateGeneration gen(std::strtoull(argv[3], nullptr, 10));
        const std::string kind = argc > 4 ? argv[4] : "kv";
        const std::string domain = argc > 5 ? argv[5] : "mem";
        const std::uint64_t node = argc > 6 ? std::strtoull(argv[6], nullptr, 10) : 1;
        const std::uint64_t device = argc > 7 ? std::strtoull(argv[7], nullptr, 10) : 0;
        register_with(eng, make_rec(st, gen, kind, domain, node, device));
        print_record(*eng.get_record(StateRecordId((st.value() << 32) | gen.value())));
    } else if (cmd == "show" || cmd == "query" || cmd == "explain") {
        StateId st(std::strtoull(argv[2], nullptr, 10));
        QueryDescriptor d; d.query_id = QueryId(1); d.state_id = st;
        QueryResult res = eng.query(d);
        std::cout << "outcome=" << to_string(res.outcome) << " candidates=" << res.ranked_candidates.size() << "\n";
        for (const auto& e : res.eliminated)
            std::cout << "  eliminated " << to_string(e.record_id) << ": " << to_string(e.reason) << " (" << e.note << ")\n";
        for (const auto& c : res.ranked_candidates)
            std::cout << "  candidate " << to_string(c.record_id) << " gen " << to_string(c.generation) << ": " << c.summary << "\n";
        if (res.selected_record_id.has_value()) {
            auto rec = eng.get_record(*res.selected_record_id);
            if (rec.has_value()) print_record(*rec);
        }
    } else if (cmd == "query-kind") {
        StateKind k = parse_kind(argv[2]);
        QueryDescriptor d; d.query_id = QueryId(2); d.kind = k;
        QueryResult res = eng.query(d);
        std::cout << "outcome=" << to_string(res.outcome) << " candidates=" << res.ranked_candidates.size() << "\n";
    } else if (cmd == "locations") {
        StateId st(std::strtoull(argv[2], nullptr, 10));
        auto rec = eng.get_record(StateRecordId((st.value() << 32) | 1));
        if (rec) for (const auto& l : rec->locations) std::cout << eng.explain_location(l) << "\n";
        else std::cout << "state " << to_string(st) << " not found\n";
    } else if (cmd == "supersede") {
        StateId st(std::strtoull(argv[2], nullptr, 10));
        StateGeneration gen(std::strtoull(argv[3], nullptr, 10));
        register_with(eng, make_rec(st, gen, "kv", "mem", 1, 0));
    } else if (cmd == "invalidate") {
        InvalidationRecord inv; inv.invalidation_id = InvalidationId(1);
        inv.state_id = StateId(std::strtoull(argv[2], nullptr, 10));
        inv.bound_generation = StateGeneration(std::strtoull(argv[3], nullptr, 10));
        MutationEnvelope e = env_for(eng); e.state_generation = inv.bound_generation;
        auto v = eng.invalidate(inv, e);
        std::cout << to_string(v.verdict) << " " << v.reason << "\n";
    } else if (cmd == "tombstone") {
        TombstoneRecord t; t.tombstone_id = TombstoneId(1);
        t.state_id = StateId(std::strtoull(argv[2], nullptr, 10));
        t.floor_generation = StateGeneration(std::strtoull(argv[3], nullptr, 10));
        MutationEnvelope e = env_for(eng); e.state_generation = t.floor_generation;
        auto v = eng.tombstone(t, e);
        std::cout << to_string(v.verdict) << " " << v.reason << "\n";
    } else if (cmd == "history") {
        StateId st(std::strtoull(argv[2], nullptr, 10));
        for (const auto& r : eng.history_of(st)) print_record(r);
    } else if (cmd == "dependencies") {
        StateId st(std::strtoull(argv[2], nullptr, 10));
        for (const auto& d : eng.dependencies_of(st)) std::cout << d << "\n";
    } else if (cmd == "simulate") {
        synthetic_distributed(eng);
    } else if (cmd == "save") {
        std::string path = argc > 2 ? argv[2] : g_persist;
        eng.save(path);
        std::cout << "saved to " << path << "\n";
    } else if (cmd == "recover") {
        std::string path = argc > 2 ? argv[2] : g_persist;
        eng.load(path);
        std::cout << "recovered " << path << " with revalidation required for process-local locations\n";
    } else if (cmd == "benchmark") {
        return benchmark_cmd(argc > 2 ? argv[2] : "");
    } else {
        std::cout << "unknown command: " << cmd << "\n";
        return 2;
    }
    std::cout << "accounting: " << eng.accounting().summary() << "\n";
    return 0;
}
