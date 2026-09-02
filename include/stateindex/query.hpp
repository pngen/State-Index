// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef STATEINDEX_QUERY_HPP
#define STATEINDEX_QUERY_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "stateindex/strong.hpp"
#include "stateindex/enums.hpp"
#include "stateindex/refs.hpp"

namespace stateindex {

// How a query treats the requested generation.
enum class GenerationRule : std::uint8_t {
    CURRENT = 0,      // must be the current authoritative generation
    MINIMUM = 1,      // must be >= minimum_generation
    EXACT = 2,        // must equal exact_generation
    ANY = 3,          // any generation (for historical/kind scan)
    AS_OF = 4,        // as-of an index generation
};

inline std::string to_string(GenerationRule r) {
    switch (r) {
        case GenerationRule::CURRENT: return "CURRENT";
        case GenerationRule::MINIMUM: return "MINIMUM";
        case GenerationRule::EXACT: return "EXACT";
        case GenerationRule::ANY: return "ANY";
        case GenerationRule::AS_OF: return "AS_OF";
    }
    return "UNKNOWN";
}

// A query requirement on a dependency target.
struct DependencyRequirement {
    StateId target_state{};
    StateGeneration minimum_generation{};
    bool required = true;

    void serialize(ByteWriter& w) const {
        target_state.serialize(w);
        minimum_generation.serialize(w);
        w.push_byte(required ? 1 : 0);
    }
    static DependencyRequirement deserialize(ByteReader& r) {
        DependencyRequirement d;
        d.target_state = StateId::deserialize(r);
        d.minimum_generation = StateGeneration::deserialize(r);
        d.required = r.read_u8() != 0;
        return d;
    }
};

// Constrains which locations a query is willing to consider.
struct LocationConstraint {
    std::vector<MemoryDomain> domains;         // empty = any
    std::vector<NodeId> nodes;                 // empty = any
    std::vector<DeviceId> devices;             // empty = any
    AccessClass preferred_access = AccessClass::UNKNOWN;  // if not UNKNOWN

    void serialize(ByteWriter& w) const {
        w.push_u64(domains.size());
        for (auto d : domains) w.push_byte(static_cast<std::uint8_t>(d));
        w.push_u64(nodes.size());
        for (auto n : nodes) n.serialize(w);
        w.push_u64(devices.size());
        for (auto d : devices) d.serialize(w);
        w.push_byte(static_cast<std::uint8_t>(preferred_access));
    }
    static LocationConstraint deserialize(ByteReader& r) {
        LocationConstraint c;
        std::uint64_t nd = r.read_u64();
        if (nd > kMaxLocationsPerRecord) throw std::runtime_error("query: domain count exceeds bound");
        for (std::uint64_t i = 0; i < nd; ++i) c.domains.push_back(static_cast<MemoryDomain>(r.read_u8()));
        std::uint64_t nn = r.read_u64();
        if (nn > kMaxLocationsPerRecord) throw std::runtime_error("query: node count exceeds bound");
        for (std::uint64_t i = 0; i < nn; ++i) c.nodes.push_back(NodeId::deserialize(r));
        std::uint64_t ndev = r.read_u64();
        if (ndev > kMaxLocationsPerRecord) throw std::runtime_error("query: device count exceeds bound");
        for (std::uint64_t i = 0; i < ndev; ++i) c.devices.push_back(DeviceId::deserialize(r));
        c.preferred_access = static_cast<AccessClass>(r.read_u8());
        return c;
    }
};

// The query descriptor expresses everything a canonical query needs. It is not
// a general SQL engine; it is a typed request against State Index.
struct QueryDescriptor {
    QueryId query_id{};
    std::optional<StateKind> kind;
    std::optional<StateId> state_id;          // exact identity when set
    bool wildcard_state_id = false;           // match any StateId
    std::optional<NamespaceId> namespace_id;
    GenerationRule generation_rule = GenerationRule::CURRENT;
    std::optional<StateGeneration> minimum_generation;
    std::optional<StateGeneration> exact_generation;
    std::optional<StateGeneration> as_of_generation;   // only for AS_OF
    CompatibilityRequirement compatibility = CompatibilityRequirement::NONE;
    std::vector<DependencyRequirement> dependency_requirements;
    std::string content_digest;               // empty = no exact content requirement
    std::optional<OwnerId> owner_id;
    std::optional<ProducerId> producer_id;
    LocationConstraint location_constraint;
    Freshness required_freshness = Freshness::UNKNOWN;   // UNKNOWN = no requirement
    Health required_health = Health::UNKNOWN;            // UNKNOWN = no requirement
    double max_retrieval_cost = 0.0;          // 0 = no limit
    std::vector<MemoryDomain> locality_preference;       // ordered
    std::uint32_t min_replica_count = 0;
    PolicyGeneration policy_generation{};
    QueryGeneration query_generation{};
    bool include_invalidated = false;         // historical-only
    bool historical_only = false;

    [[nodiscard]] bool exact_identity() const noexcept {
        return state_id.has_value() && !wildcard_state_id;
    }

    void serialize(ByteWriter& w) const {
        query_id.serialize(w);
        w.push_byte(kind ? 1 : 0); if (kind) w.push_u16(static_cast<std::uint16_t>(*kind));
        w.push_byte(state_id ? 1 : 0); if (state_id) state_id->serialize(w);
        w.push_byte(wildcard_state_id ? 1 : 0);
        w.push_byte(namespace_id ? 1 : 0); if (namespace_id) namespace_id->serialize(w);
        w.push_byte(static_cast<std::uint8_t>(generation_rule));
        w.push_byte(minimum_generation ? 1 : 0); if (minimum_generation) minimum_generation->serialize(w);
        w.push_byte(exact_generation ? 1 : 0); if (exact_generation) exact_generation->serialize(w);
        w.push_byte(as_of_generation ? 1 : 0); if (as_of_generation) as_of_generation->serialize(w);
        w.push_byte(static_cast<std::uint8_t>(compatibility));
        w.push_u64(dependency_requirements.size());
        for (const auto& d : dependency_requirements) d.serialize(w);
        w.push_string(content_digest);
        w.push_byte(owner_id ? 1 : 0); if (owner_id) owner_id->serialize(w);
        w.push_byte(producer_id ? 1 : 0); if (producer_id) producer_id->serialize(w);
        location_constraint.serialize(w);
        w.push_byte(static_cast<std::uint8_t>(required_freshness));
        w.push_byte(static_cast<std::uint8_t>(required_health));
        w.push_u64(static_cast<std::uint64_t>(max_retrieval_cost * 1000.0));
        w.push_u64(locality_preference.size());
        for (auto d : locality_preference) w.push_byte(static_cast<std::uint8_t>(d));
        w.push_u32(min_replica_count);
        policy_generation.serialize(w);
        query_generation.serialize(w);
        w.push_byte(include_invalidated ? 1 : 0);
        w.push_byte(historical_only ? 1 : 0);
    }
    static QueryDescriptor deserialize(ByteReader& r) {
        QueryDescriptor q;
        q.query_id = QueryId::deserialize(r);
        if (r.read_u8()) q.kind = static_cast<StateKind>(r.read_u16());
        if (r.read_u8()) q.state_id = StateId::deserialize(r);
        q.wildcard_state_id = r.read_u8() != 0;
        if (r.read_u8()) q.namespace_id = NamespaceId::deserialize(r);
        q.generation_rule = static_cast<GenerationRule>(r.read_u8());
        if (r.read_u8()) q.minimum_generation = StateGeneration::deserialize(r);
        if (r.read_u8()) q.exact_generation = StateGeneration::deserialize(r);
        if (r.read_u8()) q.as_of_generation = StateGeneration::deserialize(r);
        q.compatibility = static_cast<CompatibilityRequirement>(r.read_u8());
        std::uint64_t nd = r.read_u64();
        if (nd > kMaxDependenciesPerRecord) throw std::runtime_error("query: dependency requirement count exceeds bound");
        for (std::uint64_t i = 0; i < nd; ++i) q.dependency_requirements.push_back(DependencyRequirement::deserialize(r));
        q.content_digest = r.read_string();
        if (r.read_u8()) q.owner_id = OwnerId::deserialize(r);
        if (r.read_u8()) q.producer_id = ProducerId::deserialize(r);
        q.location_constraint = LocationConstraint::deserialize(r);
        q.required_freshness = static_cast<Freshness>(r.read_u8());
        q.required_health = static_cast<Health>(r.read_u8());
        q.max_retrieval_cost = static_cast<double>(r.read_u64()) / 1000.0;
        std::uint64_t nl = r.read_u64();
        if (nl > kMaxLocationsPerRecord) throw std::runtime_error("query: locality count exceeds bound");
        for (std::uint64_t i = 0; i < nl; ++i) q.locality_preference.push_back(static_cast<MemoryDomain>(r.read_u8()));
        q.min_replica_count = r.read_u32();
        q.policy_generation = PolicyGeneration::deserialize(r);
        q.query_generation = QueryGeneration::deserialize(r);
        q.include_invalidated = r.read_u8() != 0;
        q.historical_only = r.read_u8() != 0;
        return q;
    }
};

}  // namespace stateindex

#endif  // STATEINDEX_QUERY_HPP
