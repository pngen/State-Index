// SPDX-License-Identifier: Apache-2.0
#include "example_util.hpp"
int main() {
    using namespace stateindex;
    StateIndexEngine eng(CoordinatorEpoch(1)); exutil::ensure_worker(eng);
    eng.register_state(exutil::rec(StateId(70), StateGeneration(1)), exutil::env(eng.epoch(), StateGeneration(1)));
    QueryDescriptor q; q.query_id = QueryId(15); q.state_id = StateId(70);
    auto r = eng.query(q);
    std::cout << eng.explain_query(q, r);
    return 0;
}
