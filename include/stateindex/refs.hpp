// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef STATEINDEX_REFS_HPP
#define STATEINDEX_REFS_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "stateindex/strong.hpp"
#include "stateindex/enums.hpp"
#include "stateindex/bytebuf.hpp"

namespace stateindex {

// Compatibility is referenced, not owned. State Index stores the identity,
// generation, outcome metadata, and freshness. It does not infer compatibility
// from names alone and never embeds a Compatibility Registry engine.
struct CompatibilityRef {
    CompatibilityId id{};
    CompatibilityGeneration generation{};
    CompatibilityOutcome outcome = CompatibilityOutcome::UNKNOWN;
    Freshness freshness = Freshness::UNKNOWN;
    Evidence provenance = Evidence::UNKNOWN;
    std::string evidence_note;

    [[nodiscard]] bool satisfies(CompatibilityRequirement req) const noexcept {
        switch (req) {
            case CompatibilityRequirement::NONE: return true;
            case CompatibilityRequirement::EXACT: return outcome == CompatibilityOutcome::EXACT;
            case CompatibilityRequirement::COMPATIBLE:
                return outcome == CompatibilityOutcome::EXACT ||
                       outcome == CompatibilityOutcome::COMPATIBLE;
            case CompatibilityRequirement::COMPATIBLE_WITH_ADAPTATION:
                return outcome == CompatibilityOutcome::EXACT ||
                       outcome == CompatibilityOutcome::COMPATIBLE ||
                       outcome == CompatibilityOutcome::COMPATIBLE_WITH_ADAPTATION;
        }
        return false;
    }

    void serialize(ByteWriter& w) const {
        id.serialize(w);
        generation.serialize(w);
        w.push_byte(static_cast<std::uint8_t>(outcome));
        w.push_byte(static_cast<std::uint8_t>(freshness));
        w.push_byte(static_cast<std::uint8_t>(provenance));
        w.push_string(evidence_note);
    }
    static CompatibilityRef deserialize(ByteReader& r) {
        CompatibilityRef c;
        c.id = CompatibilityId::deserialize(r);
        c.generation = CompatibilityGeneration::deserialize(r);
        c.outcome = static_cast<CompatibilityOutcome>(r.read_u8());
        c.freshness = static_cast<Freshness>(r.read_u8());
        c.provenance = static_cast<Evidence>(r.read_u8());
        c.evidence_note = r.read_string();
        return c;
    }
};

// Provenance is referenced and summarized, not duplicated. State Index keeps
// enough to filter and rank but not the full derivation DAG.
struct ProvenanceRef {
    ProvenanceId id{};
    ProvenanceGeneration generation{};
    ProducerId producer{};
    StateGeneration source_generation{};
    std::string derivation_digest;   // hex; may be empty if unknown
    Evidence quality = Evidence::UNKNOWN;
    Freshness freshness = Freshness::UNKNOWN;

    void serialize(ByteWriter& w) const {
        id.serialize(w);
        generation.serialize(w);
        producer.serialize(w);
        source_generation.serialize(w);
        w.push_string(derivation_digest);
        w.push_byte(static_cast<std::uint8_t>(quality));
        w.push_byte(static_cast<std::uint8_t>(freshness));
    }
    static ProvenanceRef deserialize(ByteReader& r) {
        ProvenanceRef p;
        p.id = ProvenanceId::deserialize(r);
        p.generation = ProvenanceGeneration::deserialize(r);
        p.producer = ProducerId::deserialize(r);
        p.source_generation = StateGeneration::deserialize(r);
        p.derivation_digest = r.read_string();
        p.quality = static_cast<Evidence>(r.read_u8());
        p.freshness = static_cast<Freshness>(r.read_u8());
        return p;
    }
};

// A dependency reference. Required dependencies create invalidation edges; State
// Index supports dependency-driven invalidation but is not a Dependency Fabric.
struct DependencyRef {
    DependencyId id{};
    DependencyGeneration generation{};
    DependencyKind kind = DependencyKind::STATE;
    StateId target_state{};
    StateGeneration required_generation{};
    bool required = true;

    void serialize(ByteWriter& w) const {
        id.serialize(w);
        generation.serialize(w);
        w.push_byte(static_cast<std::uint8_t>(kind));
        target_state.serialize(w);
        required_generation.serialize(w);
        w.push_byte(required ? 1 : 0);
    }
    static DependencyRef deserialize(ByteReader& r) {
        DependencyRef d;
        d.id = DependencyId::deserialize(r);
        d.generation = DependencyGeneration::deserialize(r);
        d.kind = static_cast<DependencyKind>(r.read_u8());
        d.target_state = StateId::deserialize(r);
        d.required_generation = StateGeneration::deserialize(r);
        d.required = r.read_u8() != 0;
        return d;
    }
};

}  // namespace stateindex

#endif  // STATEINDEX_REFS_HPP
