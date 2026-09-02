// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef STATEINDEX_COST_HPP
#define STATEINDEX_COST_HPP

#include <cstdint>
#include <cstring>
#include <string>

#include "stateindex/enums.hpp"
#include "stateindex/bytebuf.hpp"

namespace stateindex {

// Retrieval economics are represented as explicit named estimates rather than a
// single opaque score. State Index ranks using these but never becomes
// Transfer Fabric. Provenance distinguishes MEASURED from REPORTED/DERIVED/
// SYNTHETIC/UNKNOWN; derived values are never called measured.
struct RetrievalEstimate {
    double local_access_cost = 0.0;   // normalized units of access effort
    double expected_latency_ms = 0.0; // expected latency in milliseconds
    double expected_bandwidth_mbps = 0.0;
    double transfer_bytes = 0.0;      // bytes that would move to retrieve
    double restore_cost = 0.0;        // if restore_required
    double recompute_cost = 0.0;      // 0 means unknown / not applicable
    double adaptation_cost = 0.0;     // 0 means unknown / not applicable
    bool staging_required = false;
    bool restore_required = false;
    Evidence evidence = Evidence::UNKNOWN;

    [[nodiscard]] bool known() const noexcept {
        return evidence != Evidence::UNKNOWN;
    }

    void serialize(ByteWriter& w) const {
        w.push_byte(static_cast<std::uint8_t>(evidence));
        append_double(w, local_access_cost);
        append_double(w, expected_latency_ms);
        append_double(w, expected_bandwidth_mbps);
        append_double(w, transfer_bytes);
        append_double(w, restore_cost);
        append_double(w, recompute_cost);
        append_double(w, adaptation_cost);
        w.push_byte(staging_required ? 1 : 0);
        w.push_byte(restore_required ? 1 : 0);
    }
    static RetrievalEstimate deserialize(ByteReader& r) {
        RetrievalEstimate e;
        e.evidence = static_cast<Evidence>(r.read_u8());
        e.local_access_cost = read_double(r);
        e.expected_latency_ms = read_double(r);
        e.expected_bandwidth_mbps = read_double(r);
        e.transfer_bytes = read_double(r);
        e.restore_cost = read_double(r);
        e.recompute_cost = read_double(r);
        e.adaptation_cost = read_double(r);
        e.staging_required = r.read_u8() != 0;
        e.restore_required = r.read_u8() != 0;
        return e;
    }

private:
    static void append_double(ByteWriter& w, double v) {
        std::uint64_t bits;
        static_assert(sizeof(bits) == sizeof(v), "double must be 64 bits");
        std::memcpy(&bits, &v, sizeof(bits));
        w.push_u64(bits);
    }
    static double read_double(ByteReader& r) {
        std::uint64_t bits = r.read_u64();
        double v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }
};

}  // namespace stateindex

#endif  // STATEINDEX_COST_HPP
