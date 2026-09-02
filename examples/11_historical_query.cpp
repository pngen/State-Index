// SPDX-License-Identifier: Apache-2.0
#include "example_util.hpp"
int main() {
    using namespace stateindex;
    StateIndexEngine eng(CoordinatorEpoch(1)); exutil::ensure_worker(eng);
    eng.register_state(exutil::rec(StateId(40), StateGeneration(1)), exutil::env(eng.epoch(), StateGeneration(1)));
    eng.register_state(exutil::rec(StateId(40), StateGeneration(2)), exutil::env(eng.epoch(), StateGeneration(2)));
    QueryDescriptor hq; hq.query_id = QueryId(11); hq.state_id = StateId(40); hq.historical_only = true;
    auto r = eng.query_historical(hq);
    QueryDescriptor cq; cq.query_id = QueryId(11); cq.state_id = StateId(40);
    auto cur = eng.query(cq);
    std::cout << "history-candidates=" << r.ranked_candidates.size()
              << " current-gen=" << to_string(cur.selected_generation.value_or(StateGeneration())) << "\n";
    return 0;
}
