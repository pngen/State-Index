// SPDX-License-Identifier: Apache-2.0
#include "example_util.hpp"
int main() {
    using namespace stateindex;
    StateIndexEngine eng(CoordinatorEpoch(1)); exutil::ensure_worker(eng);
    eng.register_state(exutil::rec(StateId(30), StateGeneration(4)), exutil::env(eng.epoch(), StateGeneration(4)));
    TombstoneRecord t; t.tombstone_id = TombstoneId(1); t.state_id = StateId(30); t.floor_generation = StateGeneration(4);
    MutationEnvelope e = exutil::env(eng.epoch(), StateGeneration(4));
    auto v = eng.tombstone(t, e);
    std::cout << "tombstone=" << to_string(v.verdict) << "\n";
    auto vr = eng.register_state(exutil::rec(StateId(30), StateGeneration(3)), exutil::env(eng.epoch(), StateGeneration(3)));
    std::cout << "resurrection-attempt=" << to_string(vr.verdict) << " (tombstone floor must win)\n";
    return 0;
}
