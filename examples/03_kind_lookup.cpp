// SPDX-License-Identifier: Apache-2.0
#include "example_util.hpp"
int main() {
    using namespace stateindex;
    StateIndexEngine eng(CoordinatorEpoch(1)); exutil::ensure_worker(eng);
    for (auto g : {1,2,3}) eng.register_state(exutil::rec(StateId(g), StateGeneration(1)), exutil::env(eng.epoch(), StateGeneration(1)));
    eng.register_state(exutil::rec(StateId(9), StateGeneration(1), "tensor"), exutil::env(eng.epoch(), StateGeneration(1)));
    QueryDescriptor q; q.query_id = QueryId(2); q.kind = StateKind::KV_STATE;
    auto r = eng.query(q);
    std::cout << "outcome=" << to_string(r.outcome) << " candidates=" << r.ranked_candidates.size() << "\n";
    return 0;
}
