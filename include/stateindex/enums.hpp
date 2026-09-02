// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef STATEINDEX_ENUMS_HPP
#define STATEINDEX_ENUMS_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <stdexcept>

namespace stateindex {

// Reusable state kinds. Designed for extension.
enum class StateKind : std::uint16_t {
    KV_STATE = 1,
    PREFIX_STATE = 2,
    TENSOR_STATE = 3,
    MODEL_ARTIFACT = 4,
    MODEL_SHARD = 5,
    ADAPTER = 6,
    COMPILED_KERNEL = 7,
    EXECUTION_GRAPH = 8,
    EXECUTION_PLAN = 9,
    CHECKPOINT = 10,
    CHECKPOINT_CHUNK = 11,
    CHECKPOINT_MANIFEST = 12,
    STORAGE_OBJECT = 13,
    BUFFER = 14,
    RUNTIME_ARTIFACT = 15,
    GENERIC_STATE = 16,
};

enum class Lifecycle : std::uint8_t {
    DISCOVERED = 0,
    AVAILABLE = 1,
    DEGRADED = 2,
    STALE = 3,
    INVALIDATED = 4,
    SUPERSEDED = 5,
    TOMBSTONED = 6,
    RETIRED = 7,
    MISSING = 8,
    FAILED = 9,
};

enum class Freshness : std::uint8_t {
    UNKNOWN = 0,
    CURRENT = 1,
    STALE = 2,
    REVALIDATION_REQUIRED = 3,
};

enum class Health : std::uint8_t {
    UNKNOWN = 0,
    HEALTHY = 1,
    DEGRADED = 2,
    UNHEALTHY = 3,
    UNAVAILABLE = 4,
};

enum class MemoryDomain : std::uint8_t {
    UNKNOWN = 0,
    CUDA_DEVICE = 1,
    HOST_PINNED = 2,
    HOST_MEMORY = 3,
    LOCAL_FILESYSTEM = 4,
    LOCAL_NVME_CLASS = 5,
    SHARED_FILESYSTEM_CLASS = 6,
    OBJECT_STORAGE_CLASS = 7,
    REMOTE_CACHE_CLASS = 8,
    SYNTHETIC_REMOTE = 9,
};

enum class StorageBackend : std::uint8_t {
    UNKNOWN = 0,
    MEMORY = 1,
    FILE = 2,
    CUDA = 3,
    SYNTHETIC = 4,
};

enum class StorageTier : std::uint8_t {
    UNKNOWN = 0,
    HOT = 1,
    WARM = 2,
    COLD = 3,
    SYNTHETIC = 4,
};

enum class AccessClass : std::uint8_t {
    UNKNOWN = 0,
    LOCAL = 1,
    NEAR = 2,
    REMOTE = 3,
    SYNTHETIC = 4,
};

// Source/evidence class for a measured or assumed fact.
enum class Evidence : std::uint8_t {
    UNKNOWN = 0,
    MEASURED = 1,
    REPORTED = 2,
    DERIVED = 3,
    SYNTHETIC = 4,
};

enum class CompatibilityOutcome : std::uint8_t {
    UNKNOWN = 0,
    NONE = 1,
    EXACT = 2,
    COMPATIBLE = 3,
    COMPATIBLE_WITH_ADAPTATION = 4,
    INCOMPATIBLE = 5,
};

// A query may require one of these compatibility acceptance levels.
enum class CompatibilityRequirement : std::uint8_t {
    NONE = 0,
    EXACT = 1,
    COMPATIBLE = 2,
    COMPATIBLE_WITH_ADAPTATION = 3,
};

enum class DependencyKind : std::uint8_t {
    UNKNOWN = 0,
    STATE = 1,
    TENSOR = 2,
    MODEL = 3,
    COMPILED_KERNEL = 4,
    EXECUTION_GRAPH = 5,
    CONFIG = 6,
};

enum class QueryOutcome : std::uint8_t {
    UNKNOWN = 0,
    FOUND_EXACT = 1,
    FOUND_COMPATIBLE = 2,
    FOUND_MULTIPLE = 3,
    NOT_FOUND = 4,
    STALE_ONLY = 5,
    INCOMPATIBLE_ONLY = 6,
    INVALIDATED_ONLY = 7,
    INSUFFICIENT_EVIDENCE = 8,
};

enum class RejectionReason : std::uint8_t {
    NONE = 0,
    WRONG_STATE_KIND = 1,
    WRONG_NAMESPACE = 2,
    STALE_GENERATION = 3,
    INVALIDATED = 4,
    TOMBSTONED = 5,
    COMPATIBILITY_MISMATCH = 6,
    DEPENDENCY_MISMATCH = 7,
    HEALTH_UNACCEPTABLE = 8,
    LOCATION_UNAVAILABLE = 9,
    FRESHNESS_BELOW_REQUIREMENT = 10,
    AUTHORITY_STALE = 11,
    CONTENT_DIGEST_MISMATCH = 12,
    MISSING_REQUIRED_DEPENDENCY = 13,
    NOT_CURRENT = 14,
    INSUFFICIENT_EVIDENCE = 15,
    SIZE_OUT_OF_RANGE = 16,
};

// Named ranking factors. There is no single opaque master score; each factor
// is explicit and weighted as part of a deterministic comparator.
enum class RankingFactor : std::uint8_t {
    EXACT_IDENTITY_MATCH = 0,
    GENERATION_RECENCY = 1,
    COMPATIBILITY_QUALITY = 2,
    LOCALITY = 3,
    RETRIEVAL_LATENCY = 4,
    RETRIEVAL_BANDWIDTH = 5,
    TRANSFER_BYTES = 6,
    RESTORE_COST = 7,
    REUSE_COST = 8,
    HEALTH = 9,
    REPLICA_COUNT = 10,
    FRESHNESS = 11,
    POLICY_PREFERENCE = 12,
    ADAPTATION_COST = 13,
};

// ------------- String conversions -------------

inline std::string to_string(StateKind k) {
    switch (k) {
        case StateKind::KV_STATE: return "KV_STATE";
        case StateKind::PREFIX_STATE: return "PREFIX_STATE";
        case StateKind::TENSOR_STATE: return "TENSOR_STATE";
        case StateKind::MODEL_ARTIFACT: return "MODEL_ARTIFACT";
        case StateKind::MODEL_SHARD: return "MODEL_SHARD";
        case StateKind::ADAPTER: return "ADAPTER";
        case StateKind::COMPILED_KERNEL: return "COMPILED_KERNEL";
        case StateKind::EXECUTION_GRAPH: return "EXECUTION_GRAPH";
        case StateKind::EXECUTION_PLAN: return "EXECUTION_PLAN";
        case StateKind::CHECKPOINT: return "CHECKPOINT";
        case StateKind::CHECKPOINT_CHUNK: return "CHECKPOINT_CHUNK";
        case StateKind::CHECKPOINT_MANIFEST: return "CHECKPOINT_MANIFEST";
        case StateKind::STORAGE_OBJECT: return "STORAGE_OBJECT";
        case StateKind::BUFFER: return "BUFFER";
        case StateKind::RUNTIME_ARTIFACT: return "RUNTIME_ARTIFACT";
        case StateKind::GENERIC_STATE: return "GENERIC_STATE";
    }
    return "UNKNOWN_KIND";
}

inline std::string to_string(Lifecycle l) {
    switch (l) {
        case Lifecycle::DISCOVERED: return "DISCOVERED";
        case Lifecycle::AVAILABLE: return "AVAILABLE";
        case Lifecycle::DEGRADED: return "DEGRADED";
        case Lifecycle::STALE: return "STALE";
        case Lifecycle::INVALIDATED: return "INVALIDATED";
        case Lifecycle::SUPERSEDED: return "SUPERSEDED";
        case Lifecycle::TOMBSTONED: return "TOMBSTONED";
        case Lifecycle::RETIRED: return "RETIRED";
        case Lifecycle::MISSING: return "MISSING";
        case Lifecycle::FAILED: return "FAILED";
    }
    return "UNKNOWN_LIFECYCLE";
}

inline std::string to_string(Freshness f) {
    switch (f) {
        case Freshness::UNKNOWN: return "UNKNOWN";
        case Freshness::CURRENT: return "CURRENT";
        case Freshness::STALE: return "STALE";
        case Freshness::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
    }
    return "UNKNOWN";
}

inline std::string to_string(Health h) {
    switch (h) {
        case Health::UNKNOWN: return "UNKNOWN";
        case Health::HEALTHY: return "HEALTHY";
        case Health::DEGRADED: return "DEGRADED";
        case Health::UNHEALTHY: return "UNHEALTHY";
        case Health::UNAVAILABLE: return "UNAVAILABLE";
    }
    return "UNKNOWN";
}

inline std::string to_string(MemoryDomain d) {
    switch (d) {
        case MemoryDomain::UNKNOWN: return "UNKNOWN";
        case MemoryDomain::CUDA_DEVICE: return "CUDA_DEVICE";
        case MemoryDomain::HOST_PINNED: return "HOST_PINNED";
        case MemoryDomain::HOST_MEMORY: return "HOST_MEMORY";
        case MemoryDomain::LOCAL_FILESYSTEM: return "LOCAL_FILESYSTEM";
        case MemoryDomain::LOCAL_NVME_CLASS: return "LOCAL_NVME_CLASS";
        case MemoryDomain::SHARED_FILESYSTEM_CLASS: return "SHARED_FILESYSTEM_CLASS";
        case MemoryDomain::OBJECT_STORAGE_CLASS: return "OBJECT_STORAGE_CLASS";
        case MemoryDomain::REMOTE_CACHE_CLASS: return "REMOTE_CACHE_CLASS";
        case MemoryDomain::SYNTHETIC_REMOTE: return "SYNTHETIC_REMOTE";
    }
    return "UNKNOWN";
}

inline std::string to_string(StorageBackend b) {
    switch (b) {
        case StorageBackend::UNKNOWN: return "UNKNOWN";
        case StorageBackend::MEMORY: return "MEMORY";
        case StorageBackend::FILE: return "FILE";
        case StorageBackend::CUDA: return "CUDA";
        case StorageBackend::SYNTHETIC: return "SYNTHETIC";
    }
    return "UNKNOWN";
}

inline std::string to_string(StorageTier t) {
    switch (t) {
        case StorageTier::UNKNOWN: return "UNKNOWN";
        case StorageTier::HOT: return "HOT";
        case StorageTier::WARM: return "WARM";
        case StorageTier::COLD: return "COLD";
        case StorageTier::SYNTHETIC: return "SYNTHETIC";
    }
    return "UNKNOWN";
}

inline std::string to_string(AccessClass a) {
    switch (a) {
        case AccessClass::UNKNOWN: return "UNKNOWN";
        case AccessClass::LOCAL: return "LOCAL";
        case AccessClass::NEAR: return "NEAR";
        case AccessClass::REMOTE: return "REMOTE";
        case AccessClass::SYNTHETIC: return "SYNTHETIC";
    }
    return "UNKNOWN";
}

inline std::string to_string(Evidence e) {
    switch (e) {
        case Evidence::UNKNOWN: return "UNKNOWN";
        case Evidence::MEASURED: return "MEASURED";
        case Evidence::REPORTED: return "REPORTED";
        case Evidence::DERIVED: return "DERIVED";
        case Evidence::SYNTHETIC: return "SYNTHETIC";
    }
    return "UNKNOWN";
}

inline std::string to_string(CompatibilityOutcome c) {
    switch (c) {
        case CompatibilityOutcome::UNKNOWN: return "UNKNOWN";
        case CompatibilityOutcome::NONE: return "NONE";
        case CompatibilityOutcome::EXACT: return "EXACT";
        case CompatibilityOutcome::COMPATIBLE: return "COMPATIBLE";
        case CompatibilityOutcome::COMPATIBLE_WITH_ADAPTATION: return "COMPATIBLE_WITH_ADAPTATION";
        case CompatibilityOutcome::INCOMPATIBLE: return "INCOMPATIBLE";
    }
    return "UNKNOWN";
}

inline std::string to_string(CompatibilityRequirement r) {
    switch (r) {
        case CompatibilityRequirement::NONE: return "NONE";
        case CompatibilityRequirement::EXACT: return "EXACT";
        case CompatibilityRequirement::COMPATIBLE: return "COMPATIBLE";
        case CompatibilityRequirement::COMPATIBLE_WITH_ADAPTATION: return "COMPATIBLE_WITH_ADAPTATION";
    }
    return "UNKNOWN";
}

inline std::string to_string(DependencyKind k) {
    switch (k) {
        case DependencyKind::UNKNOWN: return "UNKNOWN";
        case DependencyKind::STATE: return "STATE";
        case DependencyKind::TENSOR: return "TENSOR";
        case DependencyKind::MODEL: return "MODEL";
        case DependencyKind::COMPILED_KERNEL: return "COMPILED_KERNEL";
        case DependencyKind::EXECUTION_GRAPH: return "EXECUTION_GRAPH";
        case DependencyKind::CONFIG: return "CONFIG";
    }
    return "UNKNOWN";
}

inline std::string to_string(QueryOutcome o) {
    switch (o) {
        case QueryOutcome::UNKNOWN: return "UNKNOWN";
        case QueryOutcome::FOUND_EXACT: return "FOUND_EXACT";
        case QueryOutcome::FOUND_COMPATIBLE: return "FOUND_COMPATIBLE";
        case QueryOutcome::FOUND_MULTIPLE: return "FOUND_MULTIPLE";
        case QueryOutcome::NOT_FOUND: return "NOT_FOUND";
        case QueryOutcome::STALE_ONLY: return "STALE_ONLY";
        case QueryOutcome::INCOMPATIBLE_ONLY: return "INCOMPATIBLE_ONLY";
        case QueryOutcome::INVALIDATED_ONLY: return "INVALIDATED_ONLY";
        case QueryOutcome::INSUFFICIENT_EVIDENCE: return "INSUFFICIENT_EVIDENCE";
    }
    return "UNKNOWN";
}

inline std::string to_string(RejectionReason r) {
    switch (r) {
        case RejectionReason::NONE: return "NONE";
        case RejectionReason::WRONG_STATE_KIND: return "WRONG_STATE_KIND";
        case RejectionReason::WRONG_NAMESPACE: return "WRONG_NAMESPACE";
        case RejectionReason::STALE_GENERATION: return "STALE_GENERATION";
        case RejectionReason::INVALIDATED: return "INVALIDATED";
        case RejectionReason::TOMBSTONED: return "TOMBSTONED";
        case RejectionReason::COMPATIBILITY_MISMATCH: return "COMPATIBILITY_MISMATCH";
        case RejectionReason::DEPENDENCY_MISMATCH: return "DEPENDENCY_MISMATCH";
        case RejectionReason::HEALTH_UNACCEPTABLE: return "HEALTH_UNACCEPTABLE";
        case RejectionReason::LOCATION_UNAVAILABLE: return "LOCATION_UNAVAILABLE";
        case RejectionReason::FRESHNESS_BELOW_REQUIREMENT: return "FRESHNESS_BELOW_REQUIREMENT";
        case RejectionReason::AUTHORITY_STALE: return "AUTHORITY_STALE";
        case RejectionReason::CONTENT_DIGEST_MISMATCH: return "CONTENT_DIGEST_MISMATCH";
        case RejectionReason::MISSING_REQUIRED_DEPENDENCY: return "MISSING_REQUIRED_DEPENDENCY";
        case RejectionReason::NOT_CURRENT: return "NOT_CURRENT";
        case RejectionReason::INSUFFICIENT_EVIDENCE: return "INSUFFICIENT_EVIDENCE";
        case RejectionReason::SIZE_OUT_OF_RANGE: return "SIZE_OUT_OF_RANGE";
    }
    return "NONE";
}

inline std::string to_string(RankingFactor f) {
    switch (f) {
        case RankingFactor::EXACT_IDENTITY_MATCH: return "EXACT_IDENTITY_MATCH";
        case RankingFactor::GENERATION_RECENCY: return "GENERATION_RECENCY";
        case RankingFactor::COMPATIBILITY_QUALITY: return "COMPATIBILITY_QUALITY";
        case RankingFactor::LOCALITY: return "LOCALITY";
        case RankingFactor::RETRIEVAL_LATENCY: return "RETRIEVAL_LATENCY";
        case RankingFactor::RETRIEVAL_BANDWIDTH: return "RETRIEVAL_BANDWIDTH";
        case RankingFactor::TRANSFER_BYTES: return "TRANSFER_BYTES";
        case RankingFactor::RESTORE_COST: return "RESTORE_COST";
        case RankingFactor::REUSE_COST: return "REUSE_COST";
        case RankingFactor::HEALTH: return "HEALTH";
        case RankingFactor::REPLICA_COUNT: return "REPLICA_COUNT";
        case RankingFactor::FRESHNESS: return "FRESHNESS";
        case RankingFactor::POLICY_PREFERENCE: return "POLICY_PREFERENCE";
        case RankingFactor::ADAPTATION_COST: return "ADAPTATION_COST";
    }
    return "UNKNOWN";
}

// Parse a normalized case-insensitive name back to an enum. Throws on unknown.
inline StateKind parse_state_kind(std::string_view s) {
    for (int i = static_cast<int>(StateKind::KV_STATE); i <= static_cast<int>(StateKind::GENERIC_STATE); ++i) {
        StateKind k = static_cast<StateKind>(i);
        if (std::string_view(to_string(k)) == s) return k;
    }
    throw std::runtime_error("unknown state kind");
}

}  // namespace stateindex

#endif  // STATEINDEX_ENUMS_HPP
