// SPDX-License-Identifier: Apache-2.0
#include "example_util.hpp"
int main() {
    using namespace stateindex;
    CoordinatorEpoch epoch(1);
    AuthorityRegistry auth(epoch);
    auth.register_worker(WorkerId(1), WorkerBootId(100));
    MutationEnvelope ea; ea.coordinator_epoch = epoch; ea.worker_boot = WorkerBootId(100);
    auto v1 = auth.validate_state_publication(StateId(1), ea, StateGeneration(10));
    auth.set_current_authority(StateId(1), StateGeneration(10), WorkerBootId(100));
    auth.unregister_worker(WorkerBootId(100));
    auth.register_worker(WorkerId(1), WorkerBootId(200));
    MutationEnvelope eb; eb.coordinator_epoch = epoch; eb.worker_boot = WorkerBootId(200);
    auto v2 = auth.validate_state_publication(StateId(1), eb, StateGeneration(2));
    std::cout << "old-boot=" << to_string(v1.verdict) << " fresh-boot-gen2=" << to_string(v2.verdict) << " (fresh incarnation not fenced)\n";
    return 0;
}
