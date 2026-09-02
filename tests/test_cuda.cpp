// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
// Real RTX 5090 CUDA proof. Compiled by the host compiler (cl) and linked
// against CUDA::cudart. Uses real device allocation, a device write, and DMA
// copy-back (CPU parity), indexing the buffer as a CUDA_DEVICE location. A
// __global__ kernel path is available in test_cuda.cu when nvcc is used.
#include "test_util.hpp"
#include "scenarios.hpp"

#include <cstdint>
#include <vector>

#include <cuda_runtime.h>

using namespace stateindex;
using namespace siscenario;

static void cuda_location_proof() {
    StateIndexEngine eng(CoordinatorEpoch(1));
    WorkerBootId boot(1);
    eng.register_worker(WorkerId(1), boot);
    const StateId st(100);
    const int n = 1024;

    int devCount = 0;
    CHECK(cudaGetDeviceCount(&devCount) == cudaSuccess);
    CHECK(devCount >= 1);  // RTX 5090 present
    CHECK(cudaSetDevice(0) == cudaSuccess);

    // Deterministic device content: write a host pattern to the device, then
    // DMA it back and verify CPU parity.
    std::vector<float> pattern(n);
    for (int i = 0; i < n; ++i) pattern[i] = static_cast<float>(i) * 0.5f;

    float* device_ptr = nullptr;
    CHECK(cudaMalloc(&device_ptr, n * sizeof(float)) == cudaSuccess);
    CHECK(cudaMemcpy(device_ptr, pattern.data(), n * sizeof(float), cudaMemcpyHostToDevice) == cudaSuccess);
    std::vector<float> host(n);
    CHECK(cudaMemcpy(host.data(), device_ptr, n * sizeof(float), cudaMemcpyDeviceToHost) == cudaSuccess);
    bool parity = true;
    for (int i = 0; i < n; ++i) if (host[i] != pattern[i]) parity = false;
    CHECK(parity);  // CPU parity verified after a real device write + DMA back

    RecordOpts o;
    o.locations = {{MemoryDomain::CUDA_DEVICE, NodeId(1), DeviceId(0), Health::HEALTHY,
                    Freshness::CURRENT, Evidence::MEASURED, 1.0, 100000.0, 4096.0}};
    auto loc = make_location(o.locations[0], PlacementId((st.value() << 24) | 1), LocationGeneration(1));
    loc.process_handle = reinterpret_cast<std::uint64_t>(device_ptr);
    loc.process_scope = boot;
    StateRecord r = make_record(st, StateGeneration(1), o);
    r.locations = {loc};
    r.semantic_digest = r.compute_semantic_digest();
    CHECK(eng.register_state(r, env(CoordinatorEpoch(1), boot, StateGeneration(1))).verdict == MutationVerdict::ACCEPTED);

    QueryDescriptor q;
    q.query_id = QueryId(1);
    q.state_id = st;
    q.locality_preference = {MemoryDomain::CUDA_DEVICE};
    QueryResult res1 = eng.query(q);
    CHECK(res1.found());
    CHECK(res1.provenance == Evidence::MEASURED);
    CHECK(eng.invariant_check().empty());

    // Free the device buffer -> physical location no longer usable; invalidate.
    CHECK(cudaFree(device_ptr) == cudaSuccess);
    MutationEnvelope en = env(CoordinatorEpoch(1), boot, StateGeneration(1));
    auto rm = eng.remove_location(st, loc.placement_id, en);
    CHECK(rm.verdict == MutationVerdict::ACCEPTED);
    QueryResult res2 = eng.query(q);
    CHECK(!res2.found());

    // Allocate a fresh buffer under a new generation and re-register.
    float* fresh = nullptr;
    CHECK(cudaMalloc(&fresh, n * sizeof(float)) == cudaSuccess);
    CHECK(cudaMemcpy(fresh, pattern.data(), n * sizeof(float), cudaMemcpyHostToDevice) == cudaSuccess);
    std::vector<float> host2(n);
    CHECK(cudaMemcpy(host2.data(), fresh, n * sizeof(float), cudaMemcpyDeviceToHost) == cudaSuccess);
    bool parity2 = true;
    for (int i = 0; i < n; ++i) if (host2[i] != pattern[i]) parity2 = false;
    CHECK(parity2);

    RecordOpts o2;
    o2.locations = {{MemoryDomain::CUDA_DEVICE, NodeId(1), DeviceId(0), Health::HEALTHY,
                     Freshness::CURRENT, Evidence::MEASURED, 1.0, 100000.0, 4096.0}};
    auto loc2 = make_location(o2.locations[0], PlacementId((st.value() << 24) | (2ull << 8) | 1), LocationGeneration(2));
    loc2.process_handle = reinterpret_cast<std::uint64_t>(fresh);
    loc2.process_scope = boot;
    StateRecord r2 = make_record(st, StateGeneration(2), o2);
    r2.locations = {loc2};
    r2.semantic_digest = r2.compute_semantic_digest();
    CHECK(eng.register_state(r2, env(CoordinatorEpoch(1), boot, StateGeneration(2))).verdict == MutationVerdict::ACCEPTED);
    QueryResult res3 = eng.query(q);
    CHECK(res3.found());
    CHECK(res3.selected_generation == StateGeneration(2));
    CHECK(eng.invariant_check().empty());

    CHECK(cudaFree(fresh) == cudaSuccess);
    CHECK(cudaDeviceReset() == cudaSuccess);
}

int main() {
    int f = 0;
    f += sittest::run("cuda_location_proof", cuda_location_proof);
    if (f == 0) std::cout << "ALL PASS\n";
    return f == 0 ? 0 : 1;
}
