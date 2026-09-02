// SPDX-License-Identifier: Apache-2.0
#include "example_util.hpp"
int main() {
    using namespace stateindex;
    StateIndexEngine eng(CoordinatorEpoch(1)); exutil::ensure_worker(eng);
    StateRecord r = exutil::rec(StateId(60), StateGeneration(1), "tensor", "cuda", 1, 2);
    eng.register_state(r, exutil::env(eng.epoch(), StateGeneration(1)));
    QueryDescriptor q; q.query_id = QueryId(14); q.state_id = StateId(60); q.locality_preference = {MemoryDomain::CUDA_DEVICE};
    auto res = eng.query(q);
    auto rec = eng.get_record(StateRecordId(60ULL<<32 | 1));
    std::cout << "cuda-location-domain=" << (rec && !rec->locations.empty() ? to_string(rec->locations[0].domain) : "none")
              << " outcome=" << to_string(res.outcome) << "\n";
    std::cout << "(real RTX 5090 kernel parity proof runs in test_cuda)\n";
    return 0;
}
