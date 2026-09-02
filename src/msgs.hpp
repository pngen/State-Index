// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef STATEINDEX_MSGS_HPP
#define STATEINDEX_MSGS_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "stateindex/stateindex.hpp"
#include "stateindex/protocol.hpp"

namespace stateindex {

// Pack / unpack helpers for coordinator and worker payloads.

inline std::vector<std::uint8_t> pack_hello(WorkerId w, WorkerBootId boot) {
    ByteWriter p; w.serialize(p); boot.serialize(p); return p.data();
}
inline std::pair<WorkerId, WorkerBootId> unpack_hello(const std::vector<std::uint8_t>& p) {
    ByteReader r(ByteSpan{p}); auto w = WorkerId::deserialize(r); auto b = WorkerBootId::deserialize(r); return {w, b};
}
inline std::vector<std::uint8_t> pack_epoch(CoordinatorEpoch e) {
    ByteWriter p; e.serialize(p); return p.data();
}
inline CoordinatorEpoch unpack_epoch(const std::vector<std::uint8_t>& p) {
    ByteReader r(ByteSpan{p}); return CoordinatorEpoch::deserialize(r);
}
inline std::vector<std::uint8_t> pack_mutation_result(MutationVerdict v, const std::string& reason) {
    ByteWriter p; p.push_byte(static_cast<std::uint8_t>(v)); p.push_string(reason); return p.data();
}
inline std::pair<MutationVerdict, std::string> unpack_mutation_result(const std::vector<std::uint8_t>& p) {
    ByteReader r(ByteSpan{p}); auto v = static_cast<MutationVerdict>(r.read_u8()); auto s = r.read_string(); return {v, s};
}
inline std::vector<std::uint8_t> pack_register(const MutationEnvelope& e, const StateRecord& rec) {
    ByteWriter p; e.serialize(p); rec.serialize(p); return p.data();
}
inline void unpack_register(const std::vector<std::uint8_t>& p, MutationEnvelope& e, StateRecord& rec) {
    ByteReader r(ByteSpan{p}); e = MutationEnvelope::deserialize(r); rec = StateRecord::deserialize(r);
}
inline std::vector<std::uint8_t> pack_addloc(const MutationEnvelope& e, const StateId& state, const StateLocation& loc) {
    ByteWriter p; e.serialize(p); state.serialize(p); loc.serialize(p); return p.data();
}
inline void unpack_addloc(const std::vector<std::uint8_t>& p, MutationEnvelope& e, StateId& state, StateLocation& loc) {
    ByteReader r(ByteSpan{p}); e = MutationEnvelope::deserialize(r); state = StateId::deserialize(r); loc = StateLocation::deserialize(r);
}
inline std::vector<std::uint8_t> pack_rmloc(const MutationEnvelope& e, const StateId& state, const PlacementId& pl) {
    ByteWriter p; e.serialize(p); state.serialize(p); pl.serialize(p); return p.data();
}
inline void unpack_rmloc(const std::vector<std::uint8_t>& p, MutationEnvelope& e, StateId& state, PlacementId& pl) {
    ByteReader r(ByteSpan{p}); e = MutationEnvelope::deserialize(r); state = StateId::deserialize(r); pl = PlacementId::deserialize(r);
}
inline std::vector<std::uint8_t> pack_invalidate(const MutationEnvelope& e, const InvalidationRecord& inv) {
    ByteWriter p; e.serialize(p); inv.serialize(p); return p.data();
}
inline void unpack_invalidate(const std::vector<std::uint8_t>& p, MutationEnvelope& e, InvalidationRecord& inv) {
    ByteReader r(ByteSpan{p}); e = MutationEnvelope::deserialize(r); inv = InvalidationRecord::deserialize(r);
}
inline std::vector<std::uint8_t> pack_tombstone(const MutationEnvelope& e, const TombstoneRecord& t) {
    ByteWriter p; e.serialize(p); t.serialize(p); return p.data();
}
inline void unpack_tombstone(const std::vector<std::uint8_t>& p, MutationEnvelope& e, TombstoneRecord& t) {
    ByteReader r(ByteSpan{p}); e = MutationEnvelope::deserialize(r); t = TombstoneRecord::deserialize(r);
}
inline std::vector<std::uint8_t> pack_query(const QueryDescriptor& q) {
    ByteWriter p; q.serialize(p); return p.data();
}
inline QueryDescriptor unpack_query(const std::vector<std::uint8_t>& p) {
    ByteReader r(ByteSpan{p}); return QueryDescriptor::deserialize(r);
}
inline std::vector<std::uint8_t> pack_query_result(const QueryResult& q) {
    ByteWriter p; q.serialize(p); return p.data();
}
inline QueryResult unpack_query_result(const std::vector<std::uint8_t>& p) {
    ByteReader r(ByteSpan{p}); return QueryResult::deserialize(r);
}
inline std::vector<std::uint8_t> pack_path(const std::string& path) {
    ByteWriter p; p.push_string(path); return p.data();
}
inline std::string unpack_path(const std::vector<std::uint8_t>& p) {
    ByteReader r(ByteSpan{p}); return r.read_string();
}

}  // namespace stateindex

#endif  // STATEINDEX_MSGS_HPP
