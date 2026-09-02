// SPDX-License-Identifier: Apache-2.0
#include "example_util.hpp"
int main() {
    using namespace stateindex;
    StateIndexEngine eng(CoordinatorEpoch(1)); exutil::ensure_worker(eng);
    auto e = exutil::env(eng.epoch(), StateGeneration(1));
    eng.register_state(exutil::rec(StateId(5), StateGeneration(1)), e);
    QueryDescriptor q; q.query_id = QueryId(1); q.state_id = StateId(5);
    auto r = eng.query(q);
    std::cout << "outcome=" << to_string(r.outcome) << " gen=" << to_string(r.selected_generation.value_or(StateGeneration())) << "\n";
    return 0;
}
