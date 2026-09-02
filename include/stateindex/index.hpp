// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef STATEINDEX_INDEX_HPP
#define STATEINDEX_INDEX_HPP

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "stateindex/strong.hpp"
#include "stateindex/enums.hpp"
#include "stateindex/record.hpp"

namespace stateindex {

// Explicit, typed secondary indexes. The runtime avoids one giant opaque
// composite hash; each index is a key -> sorted vector of record ids. Indexes
// are rebuilt deterministically from the canonical record set on recovery.
class SecondaryIndexes {
public:
    void clear() {
        by_state_.clear(); by_kind_.clear(); by_namespace_.clear(); by_owner_.clear();
        by_producer_.clear(); by_digest_.clear(); by_compat_.clear(); by_dep_target_.clear();
        by_node_.clear(); by_device_.clear(); by_domain_.clear(); by_fresh_.clear();
        by_health_.clear(); by_lifecycle_.clear(); by_gen_.clear(); by_size_.clear();
        location_owner_.clear();
    }

    void insert(const StateRecord& rec) {
        add(by_state_, rec.state_id, rec.record_id);
        add(by_kind_, rec.kind, rec.record_id);
        add(by_namespace_, rec.namespace_id, rec.record_id);
        if (rec.owner_id) add(by_owner_, rec.owner_id, rec.record_id);
        if (rec.producer_id) add(by_producer_, rec.producer_id, rec.record_id);
        if (!rec.content_digest.empty()) add(by_digest_, rec.content_digest, rec.record_id);
        if (rec.compatibility) add(by_compat_, rec.compatibility->id, rec.record_id);
        for (const auto& d : rec.dependencies) add(by_dep_target_, d.target_state, rec.record_id);
        for (const auto& loc : rec.locations) {
            add(by_node_, loc.node_id, rec.record_id);
            if (loc.device_id) add(by_device_, loc.device_id, rec.record_id);
            add(by_domain_, loc.domain, rec.record_id);
            location_owner_[loc.placement_id] = rec.record_id;
        }
        add(by_fresh_, rec.freshness, rec.record_id);
        add(by_health_, rec.health, rec.record_id);
        add(by_lifecycle_, rec.lifecycle, rec.record_id);
        add(by_gen_, rec.state_generation, rec.record_id);
        add(by_size_, size_bucket(rec.logical_size), rec.record_id);
    }

    // Remove a record from every index it appears in. Used when a record's
    // keyed fields change (lifecycle, locations) or when a location is removed.
    void remove(const StateRecord& rec) {
        rem(by_state_, rec.state_id, rec.record_id);
        rem(by_kind_, rec.kind, rec.record_id);
        rem(by_namespace_, rec.namespace_id, rec.record_id);
        if (rec.owner_id) rem(by_owner_, rec.owner_id, rec.record_id);
        if (rec.producer_id) rem(by_producer_, rec.producer_id, rec.record_id);
        if (!rec.content_digest.empty()) rem(by_digest_, rec.content_digest, rec.record_id);
        if (rec.compatibility) rem(by_compat_, rec.compatibility->id, rec.record_id);
        for (const auto& d : rec.dependencies) rem(by_dep_target_, d.target_state, rec.record_id);
        for (const auto& loc : rec.locations) {
            rem(by_node_, loc.node_id, rec.record_id);
            if (loc.device_id) rem(by_device_, loc.device_id, rec.record_id);
            rem(by_domain_, loc.domain, rec.record_id);
            auto it = location_owner_.find(loc.placement_id);
            if (it != location_owner_.end() && it->second == rec.record_id) location_owner_.erase(it);
        }
        rem(by_fresh_, rec.freshness, rec.record_id);
        rem(by_health_, rec.health, rec.record_id);
        rem(by_lifecycle_, rec.lifecycle, rec.record_id);
        rem(by_gen_, rec.state_generation, rec.record_id);
        rem(by_size_, size_bucket(rec.logical_size), rec.record_id);
    }

    // Rebuild deterministically from a canonical record set.
    void rebuild(const std::vector<StateRecord>& records) {
        clear();
        std::vector<StateRecord> sorted = records;
        std::sort(sorted.begin(), sorted.end(),
            [](const StateRecord& a, const StateRecord& b) { return a.record_id < b.record_id; });
        for (const auto& rec : sorted) insert(rec);
    }

    // -------- Lookups (return sorted, deduplicated record ids) --------
    template <typename Key>
    std::vector<StateRecordId> lookup(const std::unordered_map<Key, std::vector<StateRecordId>>& idx,
                                      const Key& key) const {
        auto it = idx.find(key);
        if (it == idx.end()) return {};
        return it->second;
    }

    std::vector<StateRecordId> by_state(StateId s) const { return lookup(by_state_, s); }
    std::vector<StateRecordId> by_kind(StateKind k) const { return lookup(by_kind_, k); }
    std::vector<StateRecordId> by_namespace(NamespaceId n) const { return lookup(by_namespace_, n); }
    std::vector<StateRecordId> by_owner(OwnerId o) const { return lookup(by_owner_, o); }
    std::vector<StateRecordId> by_producer(ProducerId p) const { return lookup(by_producer_, p); }
    std::vector<StateRecordId> by_digest(const std::string& d) const { return lookup(by_digest_, d); }
    std::vector<StateRecordId> by_compat(CompatibilityId c) const { return lookup(by_compat_, c); }
    std::vector<StateRecordId> by_dep_target(StateId s) const { return lookup(by_dep_target_, s); }
    std::vector<StateRecordId> by_node(NodeId n) const { return lookup(by_node_, n); }
    std::vector<StateRecordId> by_device(DeviceId d) const { return lookup(by_device_, d); }
    std::vector<StateRecordId> by_domain(MemoryDomain d) const { return lookup(by_domain_, d); }
    std::vector<StateRecordId> by_freshness(Freshness f) const { return lookup(by_fresh_, f); }
    std::vector<StateRecordId> by_health(Health h) const { return lookup(by_health_, h); }
    std::vector<StateRecordId> by_lifecycle(Lifecycle l) const { return lookup(by_lifecycle_, l); }
    std::vector<StateRecordId> by_generation(StateGeneration g) const { return lookup(by_gen_, g); }

    // Locate the owning record id of a placement (or none).
    std::optional<StateRecordId> location_owner(PlacementId p) const {
        auto it = location_owner_.find(p);
        if (it == location_owner_.end()) return std::nullopt;
        return it->second;
    }

    // Verify that the indexes agree with the canonical record set. Returns
    // every index entry that does not resolve, plus any record that is missing
    // from an index it should appear in.
    std::vector<std::string> verify(const std::vector<StateRecord>& canonical) const;

private:
    std::unordered_map<StateId, std::vector<StateRecordId>> by_state_;
    std::unordered_map<StateKind, std::vector<StateRecordId>> by_kind_;
    std::unordered_map<NamespaceId, std::vector<StateRecordId>> by_namespace_;
    std::unordered_map<OwnerId, std::vector<StateRecordId>> by_owner_;
    std::unordered_map<ProducerId, std::vector<StateRecordId>> by_producer_;
    std::unordered_map<std::string, std::vector<StateRecordId>> by_digest_;
    std::unordered_map<CompatibilityId, std::vector<StateRecordId>> by_compat_;
    std::unordered_map<StateId, std::vector<StateRecordId>> by_dep_target_;
    std::unordered_map<NodeId, std::vector<StateRecordId>> by_node_;
    std::unordered_map<DeviceId, std::vector<StateRecordId>> by_device_;
    std::unordered_map<MemoryDomain, std::vector<StateRecordId>> by_domain_;
    std::unordered_map<Freshness, std::vector<StateRecordId>> by_fresh_;
    std::unordered_map<Health, std::vector<StateRecordId>> by_health_;
    std::unordered_map<Lifecycle, std::vector<StateRecordId>> by_lifecycle_;
    std::unordered_map<StateGeneration, std::vector<StateRecordId>> by_gen_;
    std::unordered_map<std::uint32_t, std::vector<StateRecordId>> by_size_;  // bucket
    std::unordered_map<PlacementId, StateRecordId> location_owner_;

    template <typename Key, typename Val>
    static void add(std::unordered_map<Key, std::vector<Val>>& m, const Key& k, const Val& v) {
        m[k].push_back(v);
        auto& vec = m[k];
        std::sort(vec.begin(), vec.end());
        vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
    }

    template <typename Key, typename Val>
    static void rem(std::unordered_map<Key, std::vector<Val>>& m, const Key& k, const Val& v) {
        auto it = m.find(k);
        if (it == m.end()) return;
        auto& vec = it->second;
        vec.erase(std::remove(vec.begin(), vec.end(), v), vec.end());
        if (vec.empty()) m.erase(it);
    }

    static std::uint32_t size_bucket(std::uint64_t size) {
        if (size == 0) return 0;
        std::uint32_t b = 0;
        while (size > 1) { size >>= 1; ++b; }
        return b;
    }
};

}  // namespace stateindex

#endif  // STATEINDEX_INDEX_HPP
