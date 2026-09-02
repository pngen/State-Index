// SPDX-License-Identifier: Apache-2.0
#include "example_util.hpp"
int main() {
    using namespace stateindex;
    StateIndexEngine eng(CoordinatorEpoch(1)); exutil::ensure_worker(eng);
    eng.register_state(exutil::rec(StateId(10), StateGeneration(1)), exutil::env(eng.epoch(), StateGeneration(1)));
    std::vector<DependencyRef> deps{DependencyRef{DependencyId(1), DependencyGeneration(1), DependencyKind::STATE, StateId(10), StateGeneration(1), true}};
    eng.register_state(exutil::rec(StateId(11), StateGeneration(1), "kv", "mem", 1, 0, std::nullopt, deps), exutil::env(eng.epoch(), StateGeneration(1)));
    QueryDescriptor q; q.query_id = QueryId(7); q.state_id = StateId(11);
    q.dependency_requirements.push_back(DependencyRequirement{StateId(10), StateGeneration(1), true});
    auto r = eng.query(q);
    std::cout << "outcome=" << to_string(r.outcome) << " found=" << r.found() << "\n";
    return 0;
}
