// SPDX-License-Identifier: Apache-2.0
#include "example_util.hpp"
#include <cstdio>
int main() {
    using namespace stateindex;
    StateIndexEngine eng(CoordinatorEpoch(1)); exutil::ensure_worker(eng);
    eng.register_state(exutil::rec(StateId(50), StateGeneration(1)), exutil::env(eng.epoch(), StateGeneration(1)));
    eng.save("example_persist.bin");
    StateIndexEngine re(CoordinatorEpoch(1)); re.load("example_persist.bin");
    QueryDescriptor q; q.query_id = QueryId(12); q.state_id = StateId(50);
    auto r = re.query(q);
    std::cout << "recovered outcome=" << to_string(r.outcome) << " invariant_ok=" << re.invariant_check().empty() << "\n";
    std::remove("example_persist.bin");
    return 0;
}
