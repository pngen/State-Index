// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef STATEINDEX_STRONG_HPP
#define STATEINDEX_STRONG_HPP

#include <compare>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <ostream>
#include <string>
#include <unordered_set>

#include "stateindex/bytebuf.hpp"

namespace stateindex {

// Identity tag structs. Each tag creates a distinct, non-interchangeable type.
struct StateIdTag {};
struct StateRecordIdTag {};
struct StateKindIdTag {};
struct NamespaceIdTag {};
struct OwnerIdTag {};
struct ProducerIdTag {};
struct WorkerIdTag {};
struct WorkerBootIdTag {};
struct NodeIdTag {};
struct DeviceIdTag {};
struct MemoryDomainIdTag {};
struct StorageBackendIdTag {};
struct StorageTierIdTag {};
struct PlacementIdTag {};
struct ReplicaIdTag {};
struct CompatibilityIdTag {};
struct ProvenanceIdTag {};
struct DependencyIdTag {};
struct QueryIdTag {};
struct QueryPlanIdTag {};
struct ResultIdTag {};
struct InvalidationIdTag {};
struct TombstoneIdTag {};
struct ObservationIdTag {};
struct AttemptIdTag {};
struct DispatchIdTag {};

// Generation tag structs.
struct CoordinatorEpochTag {};
struct StateGenerationTag {};
struct RecordGenerationTag {};
struct OwnerGenerationTag {};
struct ProducerGenerationTag {};
struct WorkerGenerationTag {};
struct LocationGenerationTag {};
struct PlacementGenerationTag {};
struct ReplicaGenerationTag {};
struct CompatibilityGenerationTag {};
struct ProvenanceGenerationTag {};
struct DependencyGenerationTag {};
struct IndexGenerationTag {};
struct QueryGenerationTag {};
struct ObservationGenerationTag {};
struct AttemptGenerationTag {};
struct DispatchGenerationTag {};
struct PolicyGenerationTag {};

// A strong, non-interchangeable 64-bit identity. Zero is the null/unknown id.
template <typename Tag>
class StrongId {
public:
    using value_type = std::uint64_t;

    constexpr StrongId() = default;
    explicit constexpr StrongId(value_type v) noexcept : value_(v) {}
    constexpr StrongId(const StrongId&) = default;
    constexpr StrongId& operator=(const StrongId&) = default;

    [[nodiscard]] constexpr value_type value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool is_null() const noexcept { return value_ == 0; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return value_ != 0; }

    constexpr StrongId next() const noexcept { return StrongId(value_ + 1); }

    friend constexpr bool operator==(StrongId a, StrongId b) noexcept { return a.value_ == b.value_; }
    friend constexpr bool operator!=(StrongId a, StrongId b) noexcept { return a.value_ != b.value_; }
    friend constexpr auto operator<=>(StrongId a, StrongId b) noexcept { return a.value_ <=> b.value_; }

    void serialize(ByteWriter& w) const { w.push_u64(value_); }
    static StrongId deserialize(ByteReader& r) { return StrongId(r.read_u64()); }

private:
    value_type value_ = 0;
};

// A strong, non-interchangeable monotonic generation counter. Zero means
// unset/unknown. Generations start at 1.
template <typename Tag>
class Generation {
public:
    using value_type = std::uint64_t;

    constexpr Generation() = default;
    explicit constexpr Generation(value_type v) noexcept : value_(v) {}
    constexpr Generation(const Generation&) = default;
    constexpr Generation& operator=(const Generation&) = default;

    [[nodiscard]] constexpr value_type value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool is_set() const noexcept { return value_ != 0; }
    [[nodiscard]] constexpr bool is_unset() const noexcept { return value_ == 0; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return value_ != 0; }

    constexpr Generation next() const noexcept { return Generation(value_ + 1); }

    friend constexpr bool operator==(Generation a, Generation b) noexcept { return a.value_ == b.value_; }
    friend constexpr bool operator!=(Generation a, Generation b) noexcept { return a.value_ != b.value_; }
    friend constexpr auto operator<=>(Generation a, Generation b) noexcept { return a.value_ <=> b.value_; }

    void serialize(ByteWriter& w) const { w.push_u64(value_); }
    static Generation deserialize(ByteReader& r) { return Generation(r.read_u64()); }

private:
    value_type value_ = 0;
};

// A WorkerBootId identifies one process incarnation. It is used to make
// authority incarnation-scoped: a larger generation from an old worker boot
// must never fence a fresh process incarnation. We model it as a strong id.
using StateId = StrongId<StateIdTag>;
using StateRecordId = StrongId<StateRecordIdTag>;
using StateKindId = StrongId<StateKindIdTag>;
using NamespaceId = StrongId<NamespaceIdTag>;
using OwnerId = StrongId<OwnerIdTag>;
using ProducerId = StrongId<ProducerIdTag>;
using WorkerId = StrongId<WorkerIdTag>;
using WorkerBootId = StrongId<WorkerBootIdTag>;
using NodeId = StrongId<NodeIdTag>;
using DeviceId = StrongId<DeviceIdTag>;
using MemoryDomainId = StrongId<MemoryDomainIdTag>;
using StorageBackendId = StrongId<StorageBackendIdTag>;
using StorageTierId = StrongId<StorageTierIdTag>;
using PlacementId = StrongId<PlacementIdTag>;
using ReplicaId = StrongId<ReplicaIdTag>;
using CompatibilityId = StrongId<CompatibilityIdTag>;
using ProvenanceId = StrongId<ProvenanceIdTag>;
using DependencyId = StrongId<DependencyIdTag>;
using QueryId = StrongId<QueryIdTag>;
using QueryPlanId = StrongId<QueryPlanIdTag>;
using ResultId = StrongId<ResultIdTag>;
using InvalidationId = StrongId<InvalidationIdTag>;
using TombstoneId = StrongId<TombstoneIdTag>;
using ObservationId = StrongId<ObservationIdTag>;
using AttemptId = StrongId<AttemptIdTag>;
using DispatchId = StrongId<DispatchIdTag>;

using CoordinatorEpoch = Generation<CoordinatorEpochTag>;
using StateGeneration = Generation<StateGenerationTag>;
using RecordGeneration = Generation<RecordGenerationTag>;
using OwnerGeneration = Generation<OwnerGenerationTag>;
using ProducerGeneration = Generation<ProducerGenerationTag>;
using WorkerGeneration = Generation<WorkerGenerationTag>;
using LocationGeneration = Generation<LocationGenerationTag>;
using PlacementGeneration = Generation<PlacementGenerationTag>;
using ReplicaGeneration = Generation<ReplicaGenerationTag>;
using CompatibilityGeneration = Generation<CompatibilityGenerationTag>;
using ProvenanceGeneration = Generation<ProvenanceGenerationTag>;
using DependencyGeneration = Generation<DependencyGenerationTag>;
using IndexGeneration = Generation<IndexGenerationTag>;
using QueryGeneration = Generation<QueryGenerationTag>;
using ObservationGeneration = Generation<ObservationGenerationTag>;
using AttemptGeneration = Generation<AttemptGenerationTag>;
using DispatchGeneration = Generation<DispatchGenerationTag>;
using PolicyGeneration = Generation<PolicyGenerationTag>;

// String conversion helpers.
template <typename Tag>
inline std::string to_string(const StrongId<Tag>& id) {
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(id.value()));
    return std::string(buf);
}

template <typename Tag>
inline std::string to_string(const Generation<Tag>& g) {
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(g.value()));
    return std::string(buf);
}

template <typename Tag>
inline std::ostream& operator<<(std::ostream& os, const StrongId<Tag>& id) {
    return os << to_string(id);
}

template <typename Tag>
inline std::ostream& operator<<(std::ostream& os, const Generation<Tag>& g) {
    return os << to_string(g);
}

}  // namespace stateindex

namespace std {
template <typename Tag>
struct hash<stateindex::StrongId<Tag>> {
    std::size_t operator()(const stateindex::StrongId<Tag>& id) const noexcept {
        return std::hash<std::uint64_t>{}(static_cast<std::uint64_t>(id.value()));
    }
};
template <typename Tag>
struct hash<stateindex::Generation<Tag>> {
    std::size_t operator()(const stateindex::Generation<Tag>& g) const noexcept {
        return std::hash<std::uint64_t>{}(static_cast<std::uint64_t>(g.value()));
    }
};
}  // namespace std

#endif  // STATEINDEX_STRONG_HPP
