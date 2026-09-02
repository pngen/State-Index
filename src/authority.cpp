// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "stateindex/authority.hpp"

namespace stateindex {

MutationVerdictDetail AuthorityRegistry::validate_state_publication(
    const StateId& state, const MutationEnvelope& env, const StateGeneration new_state_gen) const {
    // 1. Stale coordinator epoch.
    if (env.coordinator_epoch != epoch_)
        return {MutationVerdict::REJECTED_STALE_EPOCH,
                "coordinator epoch " + to_string(env.coordinator_epoch) +
                " is stale; current epoch is " + to_string(epoch_)};

    // 2. The boot incarnation must be currently live. A replay with an old
    //    WorkerBootId after a worker was killed is rejected here.
    if (!is_live(env.worker_boot))
        return {MutationVerdict::REJECTED_STALE_BOOT,
                "WorkerBootId " + to_string(env.worker_boot) + " is not a live incarnation"};

    // 3. Generation must be a valid generation.
    if (!new_state_gen.is_set())
        return {MutationVerdict::REJECTED_GENERATION_REGRESSION,
                "state generation is unset (0)"};

    // 4. A tombstone floor is absolute: it takes precedence over any
    //    incarnation-local generation comparison. A stale producer covered by a
    //    tombstone can never republish as current, even from a fresh boot.
    auto floor0 = tombstone_floor(state);
    if (floor0.has_value() && new_state_gen <= *floor0)
        return {MutationVerdict::REJECTED_TOMBSTONED,
                "state generation " + to_string(new_state_gen) +
                " is covered by tombstone floor " + to_string(*floor0)};

    auto cur = current_authority(state);
    auto floor = tombstone_floor(state);

    if (cur.has_value()) {
        const StateGeneration cur_gen = cur->first;
        const WorkerBootId cur_boot = cur->second;
        if (env.worker_boot == cur_boot) {
            // Same incarnation: generation must be strictly monotonic.
            if (new_state_gen == cur_gen)
                return {MutationVerdict::REJECTED_DUPLICATE,
                        "duplicate state generation " + to_string(new_state_gen) +
                        " from the current incarnation"};
            if (new_state_gen < cur_gen)
                return {MutationVerdict::REJECTED_GENERATION_REGRESSION,
                        "state generation " + to_string(new_state_gen) +
                        " regresses current generation " + to_string(cur_gen)};
            return {MutationVerdict::ACCEPTED, "monotonic publication within live incarnation"};
        }
        // Different incarnation.
        if (is_live(cur_boot)) {
            // Competing live incarnation: only a strictly higher generation wins.
            if (new_state_gen == cur_gen)
                return {MutationVerdict::REJECTED_DUPLICATE,
                        "duplicate generation across a competing live incarnation"};
            if (new_state_gen < cur_gen)
                return {MutationVerdict::REJECTED_GENERATION_REGRESSION,
                        "competing generation " + to_string(new_state_gen) +
                        " is below live current generation " + to_string(cur_gen)};
            return {MutationVerdict::ACCEPTED, "higher generation wins a live competition"};
        }
        // The current authority belongs to a non-live (crashed) incarnation.
        // This fresh incarnation is NOT fenced by that stale local generation;
        // it is only fenced by an explicit tombstone floor.
        if (floor.has_value() && new_state_gen <= *floor)
            return {MutationVerdict::REJECTED_TOMBSTONED,
                    "state generation " + to_string(new_state_gen) +
                    " is covered by tombstone floor " + to_string(*floor)};
        return {MutationVerdict::ACCEPTED,
                "fresh incarnation is not fenced by stale incarnation-local generation"};
    }

    // No current authority yet.
    if (floor.has_value() && new_state_gen <= *floor)
        return {MutationVerdict::REJECTED_TOMBSTONED,
                "state generation " + to_string(new_state_gen) +
                " is covered by tombstone floor " + to_string(*floor)};
    return {MutationVerdict::ACCEPTED, "first authoritative publication"};
}

}  // namespace stateindex
