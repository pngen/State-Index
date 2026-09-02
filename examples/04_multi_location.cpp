// SPDX-License-Identifier: Apache-2.0
#include "example_util.hpp"
int main() {
    using namespace stateindex;
    StateIndexEngine eng(CoordinatorEpoch(1)); exutil::ensure_worker(eng);
    StateRecord r = exutil::rec(StateId(7), StateGeneration(1));
    r.locations.push_back(exutil::loc(StateId(7), StateGeneration(1), "file", 2));
    eng.register_state(r, exutil::env(eng.epoch(), StateGeneration(1)));
    auto rec = eng.get_record(StateRecordId(7ULL<<32 | 1));
    std::cout << "locations=" << (rec ? rec->locations.size() : 0) << "\n";
    return 0;
}
