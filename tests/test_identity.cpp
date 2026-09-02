// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "test_util.hpp"
#include "stateindex/stateindex.hpp"

using namespace stateindex;

static void identity_semantics() {
    StateId a(42), b(43), a2(42);
    CHECK(a == a2);
    CHECK(a != b);
    CHECK((a < b));
    CHECK(a.is_null() == false);
    CHECK(StateId().is_null() == true);
    CHECK(a.next() == b);
    CHECK(to_string(a) == "42");
    StateGeneration g1(1), g2(2);
    CHECK(g1 < g2);
    CHECK(g2.next().value() == 3);
    CHECK(GenerationRule::CURRENT == GenerationRule(0));  // enum association
    CHECK(to_string(g2) == "2");
}

static void identity_serialize() {
    ByteWriter w;
    StateId a(123456);
    StateGeneration g(77);
    a.serialize(w);
    g.serialize(w);
    ByteReader r(ByteSpan(w.data()));
    StateId a2 = StateId::deserialize(r);
    StateGeneration g2 = StateGeneration::deserialize(r);
    CHECK(a2 == a);
    CHECK(g2 == g);
    CHECK(r.remaining() == 0);
}

static void authority_incarnation_scoping() {
    CoordinatorEpoch epoch(1);
    AuthorityRegistry auth(epoch);
    WorkerId w(7);
    WorkerBootId bootA(100);
    WorkerBootId bootB(200);
    auth.register_worker(w, bootA);
    CHECK(auth.is_live(bootA));

    MutationEnvelope ea;
    ea.coordinator_epoch = epoch;
    ea.worker_boot = bootA;

    // First publication accepted.
    auto v1 = auth.validate_state_publication(StateId(5), ea, StateGeneration(10));
    CHECK(v1.verdict == MutationVerdict::ACCEPTED);
    auth.set_current_authority(StateId(5), StateGeneration(10), bootA);

    // Same boot regression rejected.
    auto v2 = auth.validate_state_publication(StateId(5), ea, StateGeneration(9));
    CHECK(v2.verdict == MutationVerdict::REJECTED_GENERATION_REGRESSION);
    // Same boot duplicate rejected.
    auto v3 = auth.validate_state_publication(StateId(5), ea, StateGeneration(10));
    CHECK(v3.verdict == MutationVerdict::REJECTED_DUPLICATE);
    // Same boot forward accepted.
    auto v4 = auth.validate_state_publication(StateId(5), ea, StateGeneration(11));
    CHECK(v4.verdict == MutationVerdict::ACCEPTED);
    auth.set_current_authority(StateId(5), StateGeneration(11), bootA);

    // Kill bootA; fresh boot with a LOWER generation must NOT be fenced.
    auth.unregister_worker(bootA);
    auth.register_worker(w, bootB);
    CHECK(!auth.is_live(bootA));
    CHECK(auth.is_live(bootB));
    MutationEnvelope eb;
    eb.coordinator_epoch = epoch;
    eb.worker_boot = bootB;
    auto v5 = auth.validate_state_publication(StateId(5), eb, StateGeneration(2));
    CHECK(v5.verdict == MutationVerdict::ACCEPTED);  // fresh incarnation not fenced
    CHECK(v5.reason.find("fresh incarnation") != std::string::npos);
    auth.set_current_authority(StateId(5), StateGeneration(2), bootB);

    // Stale boot replay after unregister is rejected.
    auto v6 = auth.validate_state_publication(StateId(5), ea, StateGeneration(12));
    CHECK(v6.verdict == MutationVerdict::REJECTED_STALE_BOOT);

    // Stale epoch rejected.
    MutationEnvelope esc;
    esc.coordinator_epoch = CoordinatorEpoch(2);
    esc.worker_boot = bootB;
    auto v7 = auth.validate_state_publication(StateId(5), esc, StateGeneration(3));
    CHECK(v7.verdict == MutationVerdict::REJECTED_STALE_EPOCH);
}

static void authority_tombstone_floor() {
    CoordinatorEpoch epoch(1);
    AuthorityRegistry auth(epoch);
    WorkerId w(1);
    WorkerBootId boot(50);
    auth.register_worker(w, boot);
    auth.set_tombstone_floor(StateId(9), StateGeneration(8));
    MutationEnvelope e;
    e.coordinator_epoch = epoch;
    e.worker_boot = boot;
    auto v = auth.validate_state_publication(StateId(9), e, StateGeneration(8));
    CHECK(v.verdict == MutationVerdict::REJECTED_TOMBSTONED);
    auto v2 = auth.validate_state_publication(StateId(9), e, StateGeneration(9));
    CHECK(v2.verdict == MutationVerdict::ACCEPTED);
}

int main() {
    int f = 0;
    f += sittest::run("identity_semantics", identity_semantics);
    f += sittest::run("identity_serialize", identity_serialize);
    f += sittest::run("authority_incarnation_scoping", authority_incarnation_scoping);
    f += sittest::run("authority_tombstone_floor", authority_tombstone_floor);
    if (f == 0) std::cout << "ALL PASS\n";
    return f == 0 ? 0 : 1;
}
