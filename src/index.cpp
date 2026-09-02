// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "stateindex/index.hpp"

#include <unordered_set>

namespace stateindex {

namespace {
bool contains(const std::vector<StateRecordId>& v, StateRecordId id) {
    for (const auto& x : v) if (x == id) return true;
    return false;
}
template <typename Key, typename Val>
const std::vector<Val>& getv(const std::unordered_map<Key, std::vector<Val>>& m, const Key& k) {
    static const std::vector<Val> empty;
    auto it = m.find(k);
    if (it == m.end()) return empty;
    return it->second;
}
}  // namespace

std::vector<std::string> SecondaryIndexes::verify(const std::vector<StateRecord>& canonical) const {
    std::vector<std::string> errors;
    std::unordered_set<StateRecordId> known;
    for (const auto& rec : canonical) known.insert(rec.record_id);

    auto check_refs = [&](const auto& idx) {
        for (const auto& [key, ids] : idx) {
            (void)key;
            for (const auto id : ids)
                if (known.count(id) == 0)
                    errors.emplace_back("index references unknown record " + to_string(id));
        }
    };
    check_refs(by_state_);
    check_refs(by_kind_);
    check_refs(by_namespace_);
    check_refs(by_owner_);
    check_refs(by_producer_);
    check_refs(by_digest_);
    check_refs(by_compat_);
    check_refs(by_dep_target_);
    check_refs(by_node_);
    check_refs(by_device_);
    check_refs(by_domain_);
    check_refs(by_fresh_);
    check_refs(by_health_);
    check_refs(by_lifecycle_);
    check_refs(by_gen_);
    check_refs(by_size_);

    // Every record must be present in every index it qualifies for.
    for (const auto& rec : canonical) {
        const auto& sid = rec.record_id;
        const bool missing =
            !contains(getv(by_state_, rec.state_id), sid) ||
            !contains(getv(by_kind_, rec.kind), sid) ||
            !contains(getv(by_namespace_, rec.namespace_id), sid) ||
            (rec.owner_id && !contains(getv(by_owner_, rec.owner_id), sid)) ||
            (rec.producer_id && !contains(getv(by_producer_, rec.producer_id), sid)) ||
            (!rec.content_digest.empty() && !contains(getv(by_digest_, rec.content_digest), sid)) ||
            (rec.compatibility && !contains(getv(by_compat_, rec.compatibility->id), sid)) ||
            !contains(getv(by_fresh_, rec.freshness), sid) ||
            !contains(getv(by_health_, rec.health), sid) ||
            !contains(getv(by_lifecycle_, rec.lifecycle), sid) ||
            !contains(getv(by_gen_, rec.state_generation), sid) ||
            !contains(getv(by_size_, size_bucket(rec.logical_size)), sid);
        if (missing) {
            errors.emplace_back("record " + to_string(sid) + " missing from a required index");
            continue;
        }
        for (const auto& d : rec.dependencies)
            if (!contains(getv(by_dep_target_, d.target_state), sid))
                errors.emplace_back("by_dep_target missing " + to_string(sid));
        for (const auto& loc : rec.locations) {
            if (!contains(getv(by_node_, loc.node_id), sid))
                errors.emplace_back("by_node missing " + to_string(sid));
            if (loc.device_id && !contains(getv(by_device_, loc.device_id), sid))
                errors.emplace_back("by_device missing " + to_string(sid));
            if (!contains(getv(by_domain_, loc.domain), sid))
                errors.emplace_back("by_domain missing " + to_string(sid));
            auto owner = location_owner_.find(loc.placement_id);
            if (owner == location_owner_.end() || owner->second != sid)
                errors.emplace_back("location_owner mismatch for placement " + to_string(loc.placement_id));
        }
    }
    return errors;
}

}  // namespace stateindex
