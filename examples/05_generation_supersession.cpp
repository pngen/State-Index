// SPDX-License-Identifier: Apache-2.0
#include "example_util.hpp"
int main() {
    using namespace stateindex;
    StateIndexEngine eng(CoordinatorEpoch(1)); exutil::ensure_worker(eng);
    eng.register_state(exutil::rec(StateId(3), StateGeneration(1)), exutil::env(eng.epoch(), StateGeneration(1)));
    eng.register_state(exutil::rec(StateId(3), StateGeneration(2)), exutil::env(eng.epoch(), StateGeneration(2)));
    QueryDescriptor q; q.query_id = QueryId(3); q.state_id = StateId(3);
    auto r = eng.query(q);
    std::cout << "current gen=" << to_string(r.selected_generation.value_or(StateGeneration()))
              << " history=" << eng.history_of(StateId(3)).size() << "\n";
    return 0;
}
