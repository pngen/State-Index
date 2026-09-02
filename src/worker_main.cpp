// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "stateindex/stateindex.hpp"
#include "stateindex/tcp.hpp"
#include "msgs.hpp"

using namespace stateindex;

static std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string tok;
    while (ss >> tok) out.push_back(tok);
    return out;
}

// Build a StateRecord with the given fields, one location.
static StateRecord build_record(const StateId& st, const StateGeneration& gen, const std::string& kind,
                                const std::string& digest, const std::string& domain, const std::uint64_t node,
                                const std::uint64_t device, const CoordinatorEpoch& epoch, const WorkerBootId& boot) {
    StateRecord r;
    r.record_id = StateRecordId((st.value() << 32) | gen.value());
    r.state_id = st;
    r.state_generation = gen;
    r.record_generation = RecordGeneration(gen.value());
    if (kind == "tensor") r.kind = StateKind::TENSOR_STATE;
    else if (kind == "artifact") r.kind = StateKind::MODEL_ARTIFACT;
    else if (kind == "kv") r.kind = StateKind::KV_STATE;
    else r.kind = StateKind::GENERIC_STATE;
    r.namespace_id = NamespaceId(1);
    r.owner_id = OwnerId(1);
    r.producer_id = ProducerId(1);
    r.logical_size = 1024;
    r.content_digest = digest;
    r.lifecycle = Lifecycle::AVAILABLE;
    r.freshness = Freshness::CURRENT;
    r.health = Health::HEALTHY;
    r.policy_generation = PolicyGeneration(1);
    r.created_at = 1000; r.updated_at = 1000;
    r.description = "worker record";

    StateLocation l;
    l.placement_id = PlacementId((st.value() << 24) | (gen.value() << 8) | 1);
    l.replica_id = ReplicaId(l.placement_id.value());
    l.node_id = NodeId(node);
    l.device_id = device ? DeviceId(device) : DeviceId();
    l.domain = domain == "cuda" ? MemoryDomain::CUDA_DEVICE
             : domain == "file" ? MemoryDomain::LOCAL_FILESYSTEM
             : MemoryDomain::HOST_MEMORY;
    l.backend = l.domain == MemoryDomain::CUDA_DEVICE ? StorageBackend::CUDA : StorageBackend::MEMORY;
    l.tier = StorageTier::HOT;
    l.location_generation = LocationGeneration(gen.value());
    l.placement_generation = PlacementGeneration(gen.value());
    l.replica_generation = ReplicaGeneration(gen.value());
    l.address = "worker://" + std::to_string(l.placement_id.value());
    l.byte_size = 1024;
    l.access_class = AccessClass::LOCAL;
    l.health = Health::HEALTHY;
    l.freshness = Freshness::CURRENT;
    l.provenance = Evidence::MEASURED;
    l.retrieval.expected_latency_ms = 1.0;
    l.retrieval.evidence = Evidence::MEASURED;
    l.authority_epoch = epoch;
    l.authority_boot = boot;
    r.locations.push_back(l);
    r.semantic_digest = r.compute_semantic_digest();
    return r;
}

int main(int argc, char** argv) {
    if (argc < 6) {
        std::cerr << "usage: state-index-worker <host> <port> <worker-id> <boot-id> <script>\n";
        return 2;
    }
    const std::string host = argv[1];
    const int port = std::atoi(argv[2]);
    const WorkerId wid(std::strtoull(argv[3], nullptr, 10));
    const WorkerBootId boot(std::strtoull(argv[4], nullptr, 10));
    const std::string script = argv[5];

    try {
        tcp_handle conn = tcp_connect(host, port);
        send_frame(conn, MsgKind::HELLO, pack_hello(wid, boot));
        Frame ack = recv_frame(conn);
        CoordinatorEpoch ack_epoch = (ack.kind == MsgKind::HELLO_ACK) ? unpack_epoch(ack.payload) : CoordinatorEpoch(1);
        CoordinatorEpoch epoch = ack_epoch;   // may be overridden by an "epoch" command

        std::ifstream sf(script);
        std::string line;
        std::uint64_t boot_override = 0;
        while (std::getline(sf, line)) {
            if (line.empty() || line[0] == '#') continue;
            auto t = split(line);
            if (t.empty()) continue;
            const std::string& cmd = t[0];
            if (cmd == "epoch") { epoch = CoordinatorEpoch(std::strtoull(t[1].c_str(), nullptr, 10)); continue; }
            if (cmd == "boot") { boot_override = std::strtoull(t[1].c_str(), nullptr, 10); continue; }
            MutationEnvelope e;
            e.coordinator_epoch = epoch;
            e.worker_id = wid;
            e.worker_boot = boot_override ? WorkerBootId(boot_override) : boot;
            e.attempt_generation = AttemptGeneration(1);
            e.dispatch_generation = DispatchGeneration(1);

            if (cmd == "register") {
                StateId st(std::strtoull(t[1].c_str(), nullptr, 10));
                StateGeneration gen((std::strtoull(t[2].c_str(), nullptr, 10)));
                const std::string kind = t.size() > 3 ? t[3] : "generic";
                const std::string digest = t.size() > 4 ? t[4] : "";
                const std::string domain = t.size() > 5 ? t[5] : "mem";
                const std::uint64_t node = t.size() > 6 ? std::strtoull(t[6].c_str(), nullptr, 10) : 1;
                const std::uint64_t device = t.size() > 7 ? std::strtoull(t[7].c_str(), nullptr, 10) : 0;
                e.state_generation = gen;
                StateRecord rec = build_record(st, gen, kind, digest, domain, node, device, epoch, boot);
                send_frame(conn, MsgKind::REGISTER_STATE, pack_register(e, rec));
            } else if (cmd == "addloc") {
                StateId st(std::strtoull(t[1].c_str(), nullptr, 10));
                LocationGeneration lgen(std::strtoull(t[2].c_str(), nullptr, 10));
                const std::string domain = t.size() > 3 ? t[3] : "mem";
                const std::uint64_t node = t.size() > 4 ? std::strtoull(t[4].c_str(), nullptr, 10) : 1;
                const std::uint64_t device = t.size() > 5 ? std::strtoull(t[5].c_str(), nullptr, 10) : 0;
                e.location_generation = lgen;
                StateLocation loc;
                loc.placement_id = PlacementId((st.value() << 24) | (lgen.value() << 8) | 1);
                loc.replica_id = ReplicaId(loc.placement_id.value());
                loc.node_id = NodeId(node);
                loc.device_id = device ? DeviceId(device) : DeviceId();
                loc.domain = domain == "cuda" ? MemoryDomain::CUDA_DEVICE
                           : domain == "file" ? MemoryDomain::LOCAL_FILESYSTEM
                           : MemoryDomain::HOST_MEMORY;
                loc.backend = loc.domain == MemoryDomain::CUDA_DEVICE ? StorageBackend::CUDA : StorageBackend::MEMORY;
                loc.tier = StorageTier::HOT;
                loc.location_generation = lgen;
                loc.placement_generation = PlacementGeneration(lgen.value());
                loc.replica_generation = ReplicaGeneration(lgen.value());
                loc.address = "worker://" + std::to_string(loc.placement_id.value());
                loc.byte_size = 1024;
                loc.access_class = AccessClass::LOCAL;
                loc.health = Health::HEALTHY;
                loc.freshness = Freshness::CURRENT;
                loc.provenance = Evidence::MEASURED;
                loc.retrieval.evidence = Evidence::MEASURED;
                loc.authority_epoch = epoch;
                loc.authority_boot = boot;
                send_frame(conn, MsgKind::ADD_LOCATION, pack_addloc(e, st, loc));
            } else if (cmd == "invalidate") {
                InvalidationRecord inv;
                inv.invalidation_id = InvalidationId(1);
                inv.state_id = StateId(std::strtoull(t[1].c_str(), nullptr, 10));
                inv.bound_generation = StateGeneration(std::strtoull(t[2].c_str(), nullptr, 10));
                e.state_generation = inv.bound_generation;
                send_frame(conn, MsgKind::INVALIDATE, pack_invalidate(e, inv));
            } else if (cmd == "tombstone") {
                TombstoneRecord tomb;
                tomb.tombstone_id = TombstoneId(1);
                tomb.state_id = StateId(std::strtoull(t[1].c_str(), nullptr, 10));
                tomb.floor_generation = StateGeneration(std::strtoull(t[2].c_str(), nullptr, 10));
                e.state_generation = tomb.floor_generation;
                send_frame(conn, MsgKind::TOMBSTONE, pack_tombstone(e, tomb));
            } else if (cmd == "query") {
                QueryDescriptor q;
                q.query_id = QueryId(1);
                q.state_id = StateId(std::strtoull(t[1].c_str(), nullptr, 10));
                send_frame(conn, MsgKind::QUERY, pack_query(q));
            } else if (cmd == "save") {
                send_frame(conn, MsgKind::SAVE, pack_path(t[1]));
            } else if (cmd == "terminate") {
                send_frame(conn, MsgKind::TERMINATE, {});
            } else {
                continue;
            }

            Frame resp = recv_frame(conn);
            if (resp.kind == MsgKind::MUTATION_RESULT) {
                auto [v, reason] = unpack_mutation_result(resp.payload);
                std::cout << "CMD " << cmd << " VERDICT=" << to_string(v) << " (" << reason << ")\n";
            } else if (resp.kind == MsgKind::QUERY_RESULT) {
                QueryResult qr = unpack_query_result(resp.payload);
                std::cout << "CMD " << cmd << " OUTCOME=" << to_string(qr.outcome)
                          << " cnt=" << qr.ranked_candidates.size();
                if (qr.selected_generation.has_value())
                    std::cout << " SELECTED_GEN=" << to_string(*qr.selected_generation);
                std::cout << "\n";
            } else if (resp.kind == MsgKind::ACK) {
                std::cout << "CMD " << cmd << " OK\n";
            }
            if (cmd == "terminate") break;
            std::cout.flush();
        }
        tcp_close(conn);
        tcp_shutdown();
    } catch (const std::exception& ex) {
        std::cerr << "worker: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
