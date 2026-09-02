// SPDX-License-Identifier: Apache-2.0
#include "example_util.hpp"
int main() {
    using namespace stateindex;
    StateIndexEngine eng(CoordinatorEpoch(1)); exutil::ensure_worker(eng);
    CompatibilityRef exact{CompatibilityId(1), CompatibilityGeneration(1), CompatibilityOutcome::EXACT, Freshness::CURRENT, Evidence::MEASURED, ""};
    CompatibilityRef compat{CompatibilityId(2), CompatibilityGeneration(1), CompatibilityOutcome::COMPATIBLE, Freshness::CURRENT, Evidence::MEASURED, ""};
    eng.register_state(exutil::rec(StateId(1), StateGeneration(1), "kv", "remote", 2, 0, exact), exutil::env(eng.epoch(), StateGeneration(1)));
    eng.register_state(exutil::rec(StateId(2), StateGeneration(1), "kv", "mem", 1, 0, compat), exutil::env(eng.epoch(), StateGeneration(1)));
    QueryDescriptor q; q.query_id = QueryId(8); q.kind = StateKind::KV_STATE; q.locality_preference = {MemoryDomain::HOST_MEMORY};
    auto r = eng.query(q);
    std::cout << "selected=" << (r.selected_record_id ? to_string(*r.selected_record_id) : "none") << " (local-compatible wins) candidates=" << r.ranked_candidates.size() << "\n";
    return 0;
}
