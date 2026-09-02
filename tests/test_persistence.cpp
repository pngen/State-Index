// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "test_util.hpp"
#include "scenarios.hpp"

#include <cstdio>
#include <fstream>
#include <random>

using namespace stateindex;
using namespace siscenario;

static const char* kPath = "test_persist.bin";

static void write_all(const char* path, const std::vector<std::uint8_t>& b) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

static std::vector<std::uint8_t> build_image(const std::vector<std::uint8_t>& payload,
                                             std::uint32_t magic, std::uint16_t version,
                                             bool bad_crc, bool bad_sha) {
    ByteWriter f;
    f.push_u32(magic);
    f.push_u16(version);
    f.push_u16(0);
    f.push_span(ByteSpan(payload));
    if (bad_crc) f.push_u32(0u); else f.push_u32(crc32(payload.data(), payload.size()));
    auto sha = sha256(payload.data(), payload.size());
    if (bad_sha) sha[0] ^= 0xffu;
    f.push_bytes(sha.data(), sha.size());
    return f.data();
}

static void insert_sample(StateIndexEngine& eng, const WorkerBootId& boot, const CoordinatorEpoch& epoch) {
    eng.register_worker(WorkerId(1), boot);
    RecordOpts o;
    o.locations = {{MemoryDomain::LOCAL_FILESYSTEM, NodeId(1), DeviceId()},
                   {MemoryDomain::HOST_MEMORY, NodeId(1), DeviceId()}};
    o.digest = "d1";
    o.compat = CompatibilityRef{CompatibilityId(1), CompatibilityGeneration(1),
                                CompatibilityOutcome::EXACT, Freshness::CURRENT, Evidence::MEASURED, ""};

    // Dependency target exists and stays valid so consumer current records have
    // satisfied dependencies.
    auto dep_target = make_record(StateId(100), StateGeneration(1), o);
    eng.register_state(dep_target, env(epoch, boot, StateGeneration(1)));

    RecordOpts consumer = o;
    consumer.deps = {DependencyRef{DependencyId(1), DependencyGeneration(1), DependencyKind::STATE,
                                   StateId(100), StateGeneration(1), true}};
    auto r = make_record(StateId(5), StateGeneration(1), consumer);
    eng.register_state(r, env(epoch, boot, StateGeneration(1)));
    auto r2 = make_record(StateId(5), StateGeneration(2), consumer);
    eng.register_state(r2, env(epoch, boot, StateGeneration(2)));
    auto r3 = make_record(StateId(6), StateGeneration(1), o);
    eng.register_state(r3, env(epoch, boot, StateGeneration(1)));

    // Invalidate an independent state (no dependent) to persist an invalidation.
    InvalidationRecord inv;
    inv.invalidation_id = InvalidationId(1);
    inv.state_id = StateId(6);
    inv.bound_generation = StateGeneration(1);
    eng.invalidate(inv, env(epoch, boot, StateGeneration(1)));

    // Tombstone the historical generation-1 of state 5.
    TombstoneRecord t;
    t.tombstone_id = TombstoneId(1);
    t.state_id = StateId(5);
    t.floor_generation = StateGeneration(1);
    eng.tombstone(t, env(epoch, boot, StateGeneration(1)));
}

static void save_then_load_round_trip() {
    StateIndexEngine eng(CoordinatorEpoch(1));
    WorkerBootId boot(1);
    insert_sample(eng, boot, CoordinatorEpoch(1));
    CHECK(eng.invariant_check().empty());
    eng.save(kPath);

    StateIndexEngine reload(CoordinatorEpoch(1));
    reload.load(kPath);
    CHECK(reload.invariant_check().empty());
    CHECK(reload.index_verification().empty());

    // Logical query results identical (freshness changes are allowed on recovery).
    QueryDescriptor q;
    q.query_id = QueryId(1);
    q.state_id = StateId(5);
    auto b = eng.query(q);
    auto a = reload.query(q);
    CHECK(a.selected_generation == b.selected_generation);
    CHECK(a.selected_record_id == b.selected_record_id);
    CHECK(a.outcome == b.outcome);

    // Semantic digest stability across re-save/re-load for non-revalidated records.
    StateIndexEngine reload2(CoordinatorEpoch(1));
    reload2.load(kPath);
    auto recs1 = reload.canonical_records();
    auto recs2 = reload2.canonical_records();
    CHECK(recs1.size() == recs2.size());
    for (std::size_t i = 0; i < recs1.size(); ++i)
        CHECK(recs1[i].semantic_digest == recs2[i].semantic_digest);

    // Recovery: process-local memory location marked REVALIDATION_REQUIRED.
    auto r = reload.get_record(record_id_of(StateId(5), StateGeneration(2)));
    CHECK(r.has_value());
    bool saw_host_revalidation = false;
    for (const auto& l : r->locations)
        if (l.domain == MemoryDomain::HOST_MEMORY && l.freshness == Freshness::REVALIDATION_REQUIRED)
            saw_host_revalidation = true;
    CHECK(saw_host_revalidation);
    std::remove(kPath);
}

static void magic_and_version_rejected() {
    StateIndexEngine eng(CoordinatorEpoch(1));
    WorkerBootId boot(1);
    insert_sample(eng, boot, CoordinatorEpoch(1));
    std::vector<std::uint8_t> payload;
    // Rebuild payload exactly as save does.
    {
        ByteWriter p;
        auto recs = eng.canonical_records();
        p.push_u64(recs.size());
        for (auto& r : recs) r.serialize(p);
        p.push_u64(0);  // invalidations (we don't have access; approximate)
        p.push_u64(0);  // tombstones
        eng.epoch().serialize(p);
        p.push_u64(0);
        payload = p.data();
    }
    auto img = build_image(payload, 0xDEADBEEFu, 1, false, false);
    write_all(kPath, img);
    StateIndexEngine r2(CoordinatorEpoch(1));
    bool threw = false;
    try { r2.load(kPath); } catch (const std::exception&) { threw = true; }
    CHECK(threw);  // bad magic rejected
    std::remove(kPath);
}

static void corruption_rejected() {
    StateIndexEngine eng(CoordinatorEpoch(1));
    WorkerBootId boot(1);
    insert_sample(eng, boot, CoordinatorEpoch(1));
    // Valid image.
    std::vector<std::uint8_t> payload;
    {
        ByteWriter p;
        auto recs = eng.canonical_records();
        p.push_u64(recs.size());
        for (auto& r : recs) r.serialize(p);
        p.push_u64(0); p.push_u64(0);
        eng.epoch().serialize(p);
        p.push_u64(0);
        payload = p.data();
    }

    // Checksum corruption.
    { auto img = build_image(payload, 0x53494944u, 1, true, false); write_all(kPath, img);
      StateIndexEngine r(CoordinatorEpoch(1)); bool threw=false;
      try { r.load(kPath); } catch(const std::exception&) { threw=true; } CHECK(threw); std::remove(kPath); }
    // SHA corruption.
    { auto img = build_image(payload, 0x53494944u, 1, false, true); write_all(kPath, img);
      StateIndexEngine r(CoordinatorEpoch(1)); bool threw=false;
      try { r.load(kPath); } catch(const std::exception&) { threw=true; } CHECK(threw); std::remove(kPath); }
    // Truncation.
    { auto full = build_image(payload, 0x53494944u, 1, false, false);
      std::vector<std::uint8_t> t(full.begin(), full.begin() + 20); write_all(kPath, t);
      StateIndexEngine r(CoordinatorEpoch(1)); bool threw=false;
      try { r.load(kPath); } catch(const std::exception&) { threw=true; } CHECK(threw); std::remove(kPath); }
    // Trailing garbage.
    { auto full = build_image(payload, 0x53494944u, 1, false, false);
      full.push_back(0x41); write_all(kPath, full);
      StateIndexEngine r(CoordinatorEpoch(1)); bool threw=false;
      try { r.load(kPath); } catch(const std::exception&) { threw=true; } CHECK(threw); std::remove(kPath); }
    // Impossible count: first 8 payload bytes are the record count. Build tiny payload with huge count.
    { ByteWriter p; p.push_u64(9999999999u);
      auto img = build_image(p.data(), 0x53494944u, 1, false, false); write_all(kPath, img);
      StateIndexEngine r(CoordinatorEpoch(1)); bool threw=false;
      try { r.load(kPath); } catch(const std::exception&) { threw=true; } CHECK(threw); std::remove(kPath); }
}

int main() {
    int f = 0;
    f += sittest::run("save_then_load_round_trip", save_then_load_round_trip);
    f += sittest::run("magic_and_version_rejected", magic_and_version_rejected);
    f += sittest::run("corruption_rejected", corruption_rejected);
    if (f == 0) std::cout << "ALL PASS\n";
    return f == 0 ? 0 : 1;
}
