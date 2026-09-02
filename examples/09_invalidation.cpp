// SPDX-License-Identifier: Apache-2.0
#include "example_util.hpp"
int main() {
    using namespace stateindex;
    StateIndexEngine eng(CoordinatorEpoch(1)); exutil::ensure_worker(eng);
    eng.register_state(exutil::rec(StateId(20), StateGeneration(1)), exutil::env(eng.epoch(), StateGeneration(1)));
    InvalidationRecord inv; inv.invalidation_id = InvalidationId(1); inv.state_id = StateId(20); inv.bound_generation = StateGeneration(1);
    MutationEnvelope e = exutil::env(eng.epoch(), StateGeneration(1));
    auto v = eng.invalidate(inv, e);
    std::cout << "invalidate=" << to_string(v.verdict) << " " << v.reason << "\n";
    QueryDescriptor q; q.query_id = QueryId(9); q.state_id = StateId(20);
    auto r = eng.query(q);
    std::cout << "query-outcome=" << to_string(r.outcome) << " found=" << r.found() << "\n";
    return 0;
}
