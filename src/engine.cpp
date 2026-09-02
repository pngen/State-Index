// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "stateindex/engine.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include "stateindex/digest.hpp"
#include "stateindex/limits.hpp"
#include "stateindex/bytebuf.hpp"

namespace stateindex {

namespace {
constexpr std::uint32_t kPersistMagic = 0x53494944u;  // "SIID"
constexpr std::uint16_t kPersistVersion = 1;
constexpr std::uint64_t kBoundedRecord = kMaxRecordCount;

double inv(double x) { return 1.0 / (x + 1.0); }

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("persistence: cannot open " + path);
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    if (n < 0) throw std::runtime_error("persistence: cannot read " + path);
    f.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(buf.data()), n);
    return buf;
}
}  // namespace

const std::vector<RankingFactor>& StateIndexEngine::factor_order() {
    static const std::vector<RankingFactor> order = {
        RankingFactor::EXACT_IDENTITY_MATCH,
        RankingFactor::LOCALITY,
        RankingFactor::COMPATIBILITY_QUALITY,
        RankingFactor::HEALTH,
        RankingFactor::FRESHNESS,
        RankingFactor::GENERATION_RECENCY,
        RankingFactor::RETRIEVAL_LATENCY,
        RankingFactor::TRANSFER_BYTES,
        RankingFactor::RETRIEVAL_BANDWIDTH,
        RankingFactor::RESTORE_COST,
        RankingFactor::REUSE_COST,
        RankingFactor::REPLICA_COUNT,
        RankingFactor::POLICY_PREFERENCE,
        RankingFactor::ADAPTATION_COST,
    };
    return order;
}

StateIndexEngine::StateIndexEngine(CoordinatorEpoch epoch) : authority_(epoch) {}

void StateIndexEngine::register_worker(WorkerId w, WorkerBootId b) { authority_.register_worker(w, b); }
void StateIndexEngine::unregister_worker(WorkerBootId b) { authority_.unregister_worker(b); }
void StateIndexEngine::advance_epoch() { authority_.advance_epoch(); }

std::optional<StateRecord> StateIndexEngine::get_record(StateRecordId id) const {
    auto it = records_.find(id);
    if (it == records_.end()) return std::nullopt;
    return it->second;
}

std::vector<StateRecord> StateIndexEngine::canonical_records() const {
    std::vector<StateRecord> out;
    out.reserve(records_.size());
    for (const auto& [id, rec] : records_) { (void)id; out.push_back(rec); }
    std::sort(out.begin(), out.end(),
              [](const StateRecord& a, const StateRecord& b) { return a.record_id < b.record_id; });
    return out;
}

std::optional<StateRecord> StateIndexEngine::find_state(const StateId& state) const {
    auto it = current_.find(state);
    if (it == current_.end()) return std::nullopt;
    return get_record(it->second);
}

bool StateIndexEngine::is_tombstoned(const StateId& state, const StateGeneration& gen) const {
    auto it = tombstones_.find(state);
    if (it == tombstones_.end()) return false;
    return it->second.covers(gen);
}

bool StateIndexEngine::dep_satisfied(const StateRecord& rec) const {
    for (const auto& dep : rec.dependencies) {
        if (!dep.required) continue;
        auto target = find_state(dep.target_state);
        if (!target.has_value()) return false;
        if (target->state_generation < dep.required_generation) return false;
        if (target->lifecycle == Lifecycle::INVALIDATED || target->lifecycle == Lifecycle::TOMBSTONED)
            return false;
    }
    return true;
}

std::vector<StateRecord> StateIndexEngine::history_of(const StateId& state) const {
    std::vector<StateRecord> out;
    auto it = history_.find(state);
    if (it == history_.end()) return out;
    for (auto rid : it->second) {
        auto r = get_record(rid);
        if (r.has_value()) out.push_back(*r);
    }
    std::sort(out.begin(), out.end(),
              [](const StateRecord& a, const StateRecord& b) { return a.state_generation > b.state_generation; });
    return out;
}

std::vector<StateLocation> StateIndexEngine::locations_of(const StateId& state) const {
    auto rec = find_state(state);
    if (!rec.has_value()) return {};
    return rec->locations;
}

std::vector<std::string> StateIndexEngine::dependencies_of(const StateId& state) const {
    auto rec = find_state(state);
    std::vector<std::string> out;
    if (!rec.has_value()) return out;
    for (const auto& d : rec->dependencies)
        out.push_back("dep:" + to_string(d.target_state) + " gen=" + to_string(d.required_generation) +
                      (d.required ? " required" : " optional"));
    return out;
}

void StateIndexEngine::set_current(const StateRecord& rec) {
    auto cit = current_.find(rec.state_id);
    if (cit != current_.end() && cit->second == rec.record_id) {
        authority_.set_current_authority(rec.state_id, rec.state_generation, rec.authority_boot);
        return;
    }
    history_[rec.state_id].push_back(rec.record_id);
    if (cit == current_.end()) {
        current_[rec.state_id] = rec.record_id;
        authority_.set_current_authority(rec.state_id, rec.state_generation, rec.authority_boot);
        return;
    }
    auto old = get_record(cit->second);
    if (old.has_value() && old->state_generation < rec.state_generation) {
        // Supersede the previous current generation. Preserve it for history.
        StateRecord old_updated = *old;
        old_updated.lifecycle = Lifecycle::SUPERSEDED;
        old_updated.is_historical = true;
        old_updated.semantic_digest = old_updated.compute_semantic_digest();
        indexes_.remove(old_updated);
        records_[old_updated.record_id] = old_updated;
        indexes_.insert(old_updated);
        current_[rec.state_id] = rec.record_id;
        authority_.set_current_authority(rec.state_id, rec.state_generation, rec.authority_boot);
    } else if (old.has_value() && old->state_generation > rec.state_generation) {
        // New record is an older historical copy; leave current unchanged.
    }
}

MutationVerdictDetail StateIndexEngine::register_state(StateRecord rec, const MutationEnvelope& env) {
    std::lock_guard<std::mutex> lk(mtx_);
    const auto verdict = authority_.validate_state_publication(rec.state_id, env, rec.state_generation);
    if (verdict.verdict != MutationVerdict::ACCEPTED) {
        if (verdict.verdict == MutationVerdict::REJECTED_DUPLICATE)
            ++accounting_.duplicate_mutation_rejections;
        else
            ++accounting_.stale_mutation_rejections;
        return verdict;
    }
    if (records_.count(rec.record_id) != 0) {
        ++accounting_.duplicate_mutation_rejections;
        return {MutationVerdict::REJECTED_DUPLICATE, "record id " + to_string(rec.record_id) + " already exists"};
    }
    if (!rec.state_generation.is_set()) {
        ++accounting_.stale_mutation_rejections;
        return {MutationVerdict::REJECTED_GENERATION_REGRESSION, "state generation is unset"};
    }
    if (is_tombstoned(rec.state_id, rec.state_generation)) {
        ++accounting_.stale_mutation_rejections;
        return {MutationVerdict::REJECTED_TOMBSTONED,
                "tombstone floor covers generation " + to_string(rec.state_generation) + " of state " + to_string(rec.state_id)};
    }
    rec.authority_epoch = env.coordinator_epoch;
    rec.authority_boot = env.worker_boot;
    if (env.record_generation.is_set()) rec.record_generation = env.record_generation;
    rec.semantic_digest = rec.compute_semantic_digest();
    rec.is_historical = false;
    records_[rec.record_id] = rec;
    indexes_.insert(rec);
    set_current(rec);
    ++accounting_.logical_states;
    ++accounting_.current_states;
    for (const auto& dep : rec.dependencies) { (void)dep; ++accounting_.dependencies; }
    for (const auto& loc : rec.locations) {
        ++accounting_.locations;
        ++accounting_.replicas;
        if (loc.domain == MemoryDomain::CUDA_DEVICE) ++accounting_.cuda_locations;
        if (loc.domain == MemoryDomain::HOST_MEMORY || loc.domain == MemoryDomain::HOST_PINNED)
            ++accounting_.host_locations;
        if (loc.domain == MemoryDomain::LOCAL_FILESYSTEM || loc.domain == MemoryDomain::LOCAL_NVME_CLASS ||
            loc.domain == MemoryDomain::SHARED_FILESYSTEM_CLASS || loc.domain == MemoryDomain::OBJECT_STORAGE_CLASS)
            ++accounting_.storage_locations;
    }
    return {MutationVerdict::ACCEPTED, "registered state " + to_string(rec.state_id) +
            " generation " + to_string(rec.state_generation)};
}


MutationVerdictDetail StateIndexEngine::add_location(const StateId& state, StateLocation loc, const MutationEnvelope& env) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (env.coordinator_epoch != authority_.epoch())
        return {MutationVerdict::REJECTED_STALE_EPOCH, "stale coordinator epoch"};
    if (!authority_.is_live(env.worker_boot))
        return {MutationVerdict::REJECTED_STALE_BOOT, "worker boot is not live"};
    auto rec = find_state(state);
    if (!rec.has_value())
        return {MutationVerdict::REJECTED_NOT_LIVE, "state not found"};
    // Location generation must be monotonic for this placement.
    for (const auto& existing : rec->locations) {
        if (existing.placement_id == loc.placement_id) {
            if (env.location_generation.is_set() && !(loc.location_generation > existing.location_generation))
                return {MutationVerdict::REJECTED_GENERATION_REGRESSION,
                        "location generation not monotonic for placement " + to_string(loc.placement_id)};
        }
    }
    loc.authority_epoch = env.coordinator_epoch;
    loc.authority_boot = env.worker_boot;
    if (env.provenance_generation.is_set()) loc.provenance_generation = env.provenance_generation;
    std::vector<StateLocation> locs = rec->locations;
    bool replaced = false;
    for (auto& l : locs) {
        if (l.placement_id == loc.placement_id) { ++accounting_.locations; l = loc; replaced = true; }
    }
    if (!replaced) { locs.push_back(loc); ++accounting_.locations; ++accounting_.replicas; }
    StateRecord updated = *rec;
    updated.locations = locs;
    updated.semantic_digest = updated.compute_semantic_digest();
    indexes_.remove(updated);
    records_[updated.record_id] = updated;
    indexes_.insert(updated);
    return {MutationVerdict::ACCEPTED, "added location " + to_string(loc.placement_id) + " to " + to_string(state)};
}


MutationVerdictDetail StateIndexEngine::remove_location(const StateId& state, const PlacementId& placement, const MutationEnvelope& env) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (env.coordinator_epoch != authority_.epoch())
        return {MutationVerdict::REJECTED_STALE_EPOCH, "stale coordinator epoch"};
    if (!authority_.is_live(env.worker_boot))
        return {MutationVerdict::REJECTED_STALE_BOOT, "worker boot is not live"};
    auto rec = find_state(state);
    if (!rec.has_value())
        return {MutationVerdict::REJECTED_NOT_LIVE, "state not found"};
    std::vector<StateLocation> locs;
    bool found = false;
    for (const auto& l : rec->locations) {
        if (l.placement_id == placement) { found = true; continue; }
        locs.push_back(l);
    }
    if (!found) {
        // Duplicate removal is a no-op and never double-decrements accounting.
        return {MutationVerdict::ACCEPTED, "location " + to_string(placement) + " was already absent"};
    }
    StateRecord updated = *rec;
    updated.locations = locs;
    updated.semantic_digest = updated.compute_semantic_digest();
    indexes_.remove(updated);
    records_[updated.record_id] = updated;
    indexes_.insert(updated);
    --accounting_.locations;
    return {MutationVerdict::ACCEPTED, "removed location " + to_string(placement) + " from " + to_string(state)};
}


MutationVerdictDetail StateIndexEngine::invalidate(const InvalidationRecord& inv, const MutationEnvelope& env) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (env.coordinator_epoch != authority_.epoch())
        return {MutationVerdict::REJECTED_STALE_EPOCH, "stale coordinator epoch"};
    if (!authority_.is_live(env.worker_boot))
        return {MutationVerdict::REJECTED_STALE_BOOT, "worker boot is not live"};
    // A stale invalidation must never invalidate fresher state: if a current
    // record has a generation above the invalidation bound, reject as stale.
    auto rec = find_state(inv.state_id);
    if (rec.has_value() && rec->state_generation > inv.bound_generation)
        return {MutationVerdict::REJECTED_NON_MONOTONIC,
                "invalidation bound " + to_string(inv.bound_generation) +
                " is below current generation " + to_string(rec->state_generation)};
    InvalidationRecord rec_inv = inv;
    rec_inv.invalidation_seq = ++invalidation_seq_;
    rec_inv.coordinator_epoch = env.coordinator_epoch;
    rec_inv.worker_boot = env.worker_boot;
    if (env.record_generation.is_set()) rec_inv.record_generation = env.record_generation;
    invalidations_.push_back(rec_inv);
    ++accounting_.invalidations;
    // Mark any current record covered by this invalidation as INVALIDATED and
    // promote the next-highest eligible generation, if any.
    auto hist = history_of(inv.state_id);
    bool changed = false;
    for (auto& r : hist) {
        if (r.state_generation <= rec_inv.bound_generation && r.lifecycle != Lifecycle::INVALIDATED) {
            auto id = r.record_id;
            auto sr = get_record(id);
            if (sr.has_value()) {
                StateRecord u = *sr;
                u.lifecycle = Lifecycle::INVALIDATED;
                u.is_historical = true;
                u.semantic_digest = u.compute_semantic_digest();
                indexes_.remove(u);
                records_[u.record_id] = u;
                indexes_.insert(u);
                changed = true;
            }
        }
    }
    if (changed || rec.has_value()) {
        // Recompute current from remaining eligible generations (highest).
        std::optional<StateRecord> best;
        for (auto rid : history_[inv.state_id]) {
            auto cand = get_record(rid);
            if (!cand.has_value()) continue;
            const bool eligible = cand->lifecycle == Lifecycle::AVAILABLE || cand->lifecycle == Lifecycle::DEGRADED ||
                                  cand->lifecycle == Lifecycle::DISCOVERED;
            if (!eligible || is_tombstoned(cand->state_id, cand->state_generation)) continue;
            if (cand->state_generation <= rec_inv.bound_generation) continue;
            if (!best.has_value() || cand->state_generation > best->state_generation) best = cand;
        }
        if (best.has_value()) {
            current_[inv.state_id] = best->record_id;
            ++accounting_.current_states;
            authority_.set_current_authority(inv.state_id, best->state_generation, best->authority_boot);
        } else {
            current_.erase(inv.state_id);
            authority_.clear_current_authority(inv.state_id);
        }
    }
    return {MutationVerdict::ACCEPTED, "invalidated state " + to_string(inv.state_id) +
            " through generation " + to_string(inv.bound_generation)};
}


MutationVerdictDetail StateIndexEngine::tombstone(const TombstoneRecord& t, const MutationEnvelope& env) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (env.coordinator_epoch != authority_.epoch())
        return {MutationVerdict::REJECTED_STALE_EPOCH, "stale coordinator epoch"};
    if (!authority_.is_live(env.worker_boot))
        return {MutationVerdict::REJECTED_STALE_BOOT, "worker boot is not live"};
    TombstoneRecord t2 = t;
    t2.coordinator_epoch = env.coordinator_epoch;
    t2.worker_boot = env.worker_boot;
    auto it = tombstones_.find(t.state_id);
    if (it != tombstones_.end() && it->second.floor_generation >= t2.floor_generation)
        return {MutationVerdict::REJECTED_STALE_BOOT,
                "existing tombstone floor " + to_string(it->second.floor_generation) +
                " already covers " + to_string(t2.floor_generation)};
    tombstones_[t.state_id] = t2;
    authority_.set_tombstone_floor(t.state_id, t2.floor_generation);
    ++accounting_.tombstones;
    // Mark covered records TOMBSTONED and remove current if covered.
    for (auto rid : history_[t.state_id]) {
        auto rec = get_record(rid);
        if (!rec.has_value()) continue;
        if (rec->state_generation <= t2.floor_generation && rec->lifecycle != Lifecycle::TOMBSTONED) {
            StateRecord u = *rec;
            u.lifecycle = Lifecycle::TOMBSTONED;
            u.is_historical = true;
            u.semantic_digest = u.compute_semantic_digest();
            indexes_.remove(u);
            records_[u.record_id] = u;
            indexes_.insert(u);
        }
    }
    auto cur = current_.find(t.state_id);
    if (cur != current_.end() && is_tombstoned(t.state_id, get_record(cur->second)->state_generation)) {
        current_.erase(cur);
        authority_.clear_current_authority(t.state_id);
    }
    return {MutationVerdict::ACCEPTED, "tombstoned state " + to_string(t.state_id) +
            " through generation " + to_string(t2.floor_generation)};
}

void StateIndexEngine::recompute_current() {
    current_.clear();
    std::unordered_map<StateId, StateGeneration> max_gen;
    for (const auto& [id, rec] : records_) {
        (void)id;
        if (rec.lifecycle == Lifecycle::INVALIDATED || rec.lifecycle == Lifecycle::TOMBSTONED ||
            rec.lifecycle == Lifecycle::SUPERSEDED)
            continue;
        if (is_tombstoned(rec.state_id, rec.state_generation)) continue;
        auto it = max_gen.find(rec.state_id);
        if (it == max_gen.end() || rec.state_generation > it->second) {
            max_gen[rec.state_id] = rec.state_generation;
            current_[rec.state_id] = rec.record_id;
        }
    }
    // Mark non-current records historical.
    for (auto& [id, rec] : records_) {
        (void)id;
        auto it = current_.find(rec.state_id);
        if (it != current_.end() && it->second == rec.record_id) rec.is_historical = false;
        else rec.is_historical = true;
    }
}

std::vector<StateRecordId> StateIndexEngine::initial_candidate_ids(const QueryDescriptor& q) const {
    if (q.state_id.has_value()) {
        std::vector<StateRecordId> all = indexes_.by_state(*q.state_id);
        std::sort(all.begin(), all.end());
        return all;
    }
    std::vector<StateRecordId> ids;
    if (q.kind.has_value()) {
        ids = indexes_.by_kind(*q.kind);
    } else {
        for (const auto& [id, rec] : records_) {
            (void)rec; ids.push_back(id);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

namespace {
double freshness_score(Freshness f) {
    switch (f) {
        case Freshness::CURRENT: return 3.0;
        case Freshness::REVALIDATION_REQUIRED: return 2.0;
        case Freshness::STALE: return 1.0;
        case Freshness::UNKNOWN: return 0.0;
    }
    return 0.0;
}
double health_score(Health h) {
    switch (h) {
        case Health::HEALTHY: return 3.0;
        case Health::DEGRADED: return 2.0;
        case Health::UNKNOWN: return 1.0;
        case Health::UNHEALTHY: return 0.0;
        case Health::UNAVAILABLE: return 0.0;
    }
    return 0.0;
}
double compat_score(CompatibilityOutcome o) {
    switch (o) {
        case CompatibilityOutcome::EXACT: return 3.0;
        case CompatibilityOutcome::COMPATIBLE: return 2.0;
        case CompatibilityOutcome::COMPATIBLE_WITH_ADAPTATION: return 1.0;
        default: return 0.0;
    }
}
}  // namespace


CandidateAssessment StateIndexEngine::evaluate_candidate(const StateRecord& rec, const QueryDescriptor& q) const {
    CandidateAssessment c;
    c.record_id = rec.record_id;
    c.generation = rec.state_generation;
    const auto reject = [&](RejectionReason r, std::string note) {
        c.in_scope = false;
        c.rejection = r;
        c.rejection_note = std::move(note);
        return c;
    };

    // Hard filters: state kind, namespace.
    if (q.kind.has_value() && rec.kind != *q.kind)
        return reject(RejectionReason::WRONG_STATE_KIND, "kind does not match requested kind");
    if (q.namespace_id.has_value() && rec.namespace_id != *q.namespace_id)
        return reject(RejectionReason::WRONG_NAMESPACE, "namespace does not match requested namespace");
    if (q.owner_id.has_value() && rec.owner_id != *q.owner_id)
        return reject(RejectionReason::INSUFFICIENT_EVIDENCE, "owner does not match requested owner");
    if (q.producer_id.has_value() && rec.producer_id != *q.producer_id)
        return reject(RejectionReason::INSUFFICIENT_EVIDENCE, "producer does not match requested producer");

    // Exact state identity must match when specified.
    if (q.state_id.has_value() && !q.wildcard_state_id && rec.state_id != *q.state_id)
        return reject(RejectionReason::WRONG_STATE_KIND, "state id does not match requested identity");

    // Content digest.
    if (!q.content_digest.empty() && rec.content_digest != q.content_digest)
        return reject(RejectionReason::CONTENT_DIGEST_MISMATCH, "content digest does not match requirement");

    // Tombstone and invalidation filters precede the generation rule so a
    // tombstoned record is reported as such rather than merely "not current".
    if (is_tombstoned(rec.state_id, rec.state_generation))
        return reject(RejectionReason::TOMBSTONED, "generation covered by a tombstone");
    if (rec.lifecycle == Lifecycle::INVALIDATED || rec.lifecycle == Lifecycle::TOMBSTONED)
        return reject(RejectionReason::INVALIDATED, "record lifecycle is " + std::string(to_string(rec.lifecycle)));
    if (!historical_mode_ && !q.include_invalidated &&
        (rec.lifecycle == Lifecycle::SUPERSEDED || rec.lifecycle == Lifecycle::RETIRED || rec.lifecycle == Lifecycle::MISSING ||
         rec.lifecycle == Lifecycle::FAILED))
        return reject(RejectionReason::NOT_CURRENT, "record is " + std::string(to_string(rec.lifecycle)));

    // Generation rule.
    const bool is_current_rec = current_.count(rec.state_id) != 0 && current_.at(rec.state_id) == rec.record_id;
    bool is_current = is_current_rec;
    switch (q.generation_rule) {
        case GenerationRule::CURRENT:
            if (!historical_mode_ && !is_current)
                return reject(RejectionReason::NOT_CURRENT,
                              "generation " + to_string(rec.state_generation) + " is not the current authority");
            break;
        case GenerationRule::MINIMUM:
            if (q.minimum_generation.has_value() && rec.state_generation < *q.minimum_generation)
                return reject(RejectionReason::STALE_GENERATION,
                              "generation " + to_string(rec.state_generation) + " below minimum " + to_string(*q.minimum_generation));
            break;
        case GenerationRule::EXACT:
            if (q.exact_generation.has_value() && rec.state_generation != *q.exact_generation)
                return reject(RejectionReason::STALE_GENERATION,
                              "generation " + to_string(rec.state_generation) + " does not equal requested " + to_string(*q.exact_generation));
            break;
        case GenerationRule::AS_OF:
            if (q.as_of_generation.has_value() && rec.state_generation > *q.as_of_generation)
                return reject(RejectionReason::NOT_CURRENT,
                              "generation " + to_string(rec.state_generation) + " is after as-of generation " + to_string(*q.as_of_generation));
            break;
        case GenerationRule::ANY:
            break;
    }

    // Compatibility requirement.
    if (q.compatibility != CompatibilityRequirement::NONE) {
        if (!rec.compatibility.has_value())
            return reject(RejectionReason::INSUFFICIENT_EVIDENCE,
                          "no compatibility evidence; cannot satisfy a compatibility requirement");
        if (rec.compatibility->outcome == CompatibilityOutcome::UNKNOWN)
            return reject(RejectionReason::INSUFFICIENT_EVIDENCE, "compatibility outcome is UNKNOWN");
        if (!rec.compatibility->satisfies(q.compatibility))
            return reject(RejectionReason::COMPATIBILITY_MISMATCH,
                          "compatibility outcome " + std::string(to_string(rec.compatibility->outcome)) +
                          " does not satisfy requirement");
    }

    // Dependency requirements requested by the query.
    for (const auto& req : q.dependency_requirements) {
        bool found = false;
        for (const auto& dep : rec.dependencies) {
            if (dep.target_state != req.target_state) continue;
            found = true;
            auto trec = find_state(req.target_state);
            if (!trec.has_value() || trec->state_generation < req.minimum_generation)
                return reject(RejectionReason::DEPENDENCY_MISMATCH,
                              "dependency " + to_string(req.target_state) + " not at required generation");
        }
        if (!found && req.required)
            return reject(RejectionReason::MISSING_REQUIRED_DEPENDENCY,
                          "required dependency " + to_string(req.target_state) + " is absent");
    }

    // Dependency-driven invalidation: a required dependency that is invalid or
    // stale makes this record ineligible.
    if (!dep_satisfied(rec))
        return reject(RejectionReason::DEPENDENCY_MISMATCH,
                      "a required dependency is invalidated, tombstoned, or stale");

    // Location constraints. Find a suitable location.
    std::vector<const StateLocation*> usable;
    for (const auto& l : rec.locations) {
        if (!l.usable()) continue;
        if (!q.location_constraint.domains.empty() &&
            std::find(q.location_constraint.domains.begin(), q.location_constraint.domains.end(), l.domain) == q.location_constraint.domains.end())
            continue;
        if (!q.location_constraint.nodes.empty() &&
            std::find(q.location_constraint.nodes.begin(), q.location_constraint.nodes.end(), l.node_id) == q.location_constraint.nodes.end())
            continue;
        if (!q.location_constraint.devices.empty() &&
            std::find(q.location_constraint.devices.begin(), q.location_constraint.devices.end(), l.device_id) == q.location_constraint.devices.end())
            continue;
        usable.push_back(&l);
    }
    if (usable.empty())
        return reject(RejectionReason::LOCATION_UNAVAILABLE, "no usable location matching the location constraints");
    if (q.required_health != Health::UNKNOWN) {
        bool ok = false;
        for (auto* l : usable) if (l->health == q.required_health) ok = true;
        if (!ok) return reject(RejectionReason::HEALTH_UNACCEPTABLE, "no location has the required health");
    }

    // Choose best placement: prefer locality preference, then health, then low latency.
    const StateLocation* best = nullptr;
    for (auto* l : usable) {
        if (best == nullptr) { best = l; continue; }
        auto rank_of = [&](const StateLocation* x) -> double {
            double s = 0.0;
            for (std::size_t i = 0; i < q.locality_preference.size(); ++i)
                if (x->domain == q.locality_preference[i]) s += double(q.locality_preference.size() - i);
            if (x->access_class == AccessClass::LOCAL) s += 10.0;
            s += health_score(x->health);
            s += 1.0 / (x->retrieval.expected_latency_ms + 1.0);
            return s;
        };
        if (rank_of(l) > rank_of(best)) best = l;
    }

    // Freshness requirement.
    if (q.required_freshness != Freshness::UNKNOWN &&
        freshness_score(rec.freshness) < freshness_score(q.required_freshness))
        return reject(RejectionReason::FRESHNESS_BELOW_REQUIREMENT,
                      "freshness " + std::string(to_string(rec.freshness)) + " is below requirement");

    // Min replica count.
    if (q.min_replica_count > 0 && static_cast<std::uint32_t>(usable.size()) < q.min_replica_count)
        return reject(RejectionReason::LOCATION_UNAVAILABLE, "replica count below required minimum");

    // Max retrieval cost.
    if (q.max_retrieval_cost > 0.0 && best->retrieval.local_access_cost > q.max_retrieval_cost)
        return reject(RejectionReason::LOCATION_UNAVAILABLE,
                      "retrieval cost exceeds allowed maximum");

    // Named ranking factors.
    c.in_scope = true;
    c.best_placement = best->placement_id;
    c.factors.resize(factor_order().size(), 0.0);
    c.factor_notes.resize(factor_order().size());
    const bool exact_id = q.state_id.has_value() && !q.wildcard_state_id && rec.state_id == *q.state_id;
    for (std::size_t i = 0; i < factor_order().size(); ++i) {
        switch (factor_order()[i]) {
            case RankingFactor::EXACT_IDENTITY_MATCH:
                c.factors[i] = exact_id ? 1.0 : 0.0;
                c.factor_notes[i] = exact_id ? "exact identity match" : "identity not exact";
                break;
            case RankingFactor::COMPATIBILITY_QUALITY:
                c.factors[i] = rec.compatibility ? compat_score(rec.compatibility->outcome) : 0.0;
                c.factor_notes[i] = rec.compatibility ? std::string(to_string(rec.compatibility->outcome)) : "no compatibility evidence";
                break;
            case RankingFactor::HEALTH:
                c.factors[i] = health_score(best->health);
                c.factor_notes[i] = std::string(to_string(best->health));
                break;
            case RankingFactor::FRESHNESS:
                c.factors[i] = freshness_score(best->freshness);
                c.factor_notes[i] = std::string(to_string(best->freshness));
                break;
            case RankingFactor::LOCALITY: {
                double s = 0.0;
                for (std::size_t j = 0; j < q.locality_preference.size(); ++j)
                    if (best->domain == q.locality_preference[j]) s += double(q.locality_preference.size() - j);
                if (best->access_class == AccessClass::LOCAL) s += 10.0;
                c.factors[i] = s;
                c.factor_notes[i] = std::string(to_string(best->domain)) + " access=" + to_string(best->access_class);
                break;
            }
            case RankingFactor::GENERATION_RECENCY:
                c.factors[i] = static_cast<double>(rec.state_generation.value());
                c.factor_notes[i] = "generation " + to_string(rec.state_generation);
                break;
            case RankingFactor::RETRIEVAL_LATENCY:
                c.factors[i] = inv(best->retrieval.expected_latency_ms);
                c.factor_notes[i] = "latency " + std::to_string(best->retrieval.expected_latency_ms) + "ms";
                break;
            case RankingFactor::TRANSFER_BYTES:
                c.factors[i] = inv(best->retrieval.transfer_bytes);
                c.factor_notes[i] = "transfer " + std::to_string(best->retrieval.transfer_bytes) + "B";
                break;
            case RankingFactor::RETRIEVAL_BANDWIDTH:
                c.factors[i] = best->retrieval.expected_bandwidth_mbps;
                c.factor_notes[i] = "bandwidth " + std::to_string(best->retrieval.expected_bandwidth_mbps) + "MB/s";
                break;
            case RankingFactor::RESTORE_COST:
                c.factors[i] = inv(best->retrieval.restore_cost);
                c.factor_notes[i] = "restore cost " + std::to_string(best->retrieval.restore_cost);
                break;
            case RankingFactor::REUSE_COST:
                c.factors[i] = inv(best->retrieval.recompute_cost);
                c.factor_notes[i] = "reuse cost " + std::to_string(best->retrieval.recompute_cost);
                break;
            case RankingFactor::REPLICA_COUNT:
                c.factors[i] = static_cast<double>(rec.locations.size());
                c.factor_notes[i] = std::to_string(rec.locations.size()) + " replicas";
                break;
            case RankingFactor::POLICY_PREFERENCE:
                c.factors[i] = q.policy_generation.is_set() && rec.policy_generation == q.policy_generation ? 1.0 : 0.0;
                c.factor_notes[i] = q.policy_generation.is_set() ? "policy match" : "policy not compared";
                break;
            case RankingFactor::ADAPTATION_COST:
                c.factors[i] = inv(best->retrieval.adaptation_cost);
                c.factor_notes[i] = "adaptation cost " + std::to_string(best->retrieval.adaptation_cost);
                break;
        }
    }
    return c;
}


namespace {
bool candidate_less(const CandidateAssessment& a, const CandidateAssessment& b) {
    const auto& order = StateIndexEngine::factor_order();
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (a.factors[i] != b.factors[i]) return a.factors[i] > b.factors[i];
    }
    if (a.generation != b.generation) return a.generation > b.generation;
    return a.record_id < b.record_id;
}
}  // namespace

QueryResult StateIndexEngine::run_query(const QueryDescriptor& q, const bool historical) {
    historical_mode_ = historical;
    QueryResult result;
    result.query_id = q.query_id;
    result.query_generation = q.query_generation;
    result.query_plan_id = QueryPlanId();

    const std::vector<StateRecordId> ids = initial_candidate_ids(q);
    std::vector<CandidateAssessment> in_scope;
    std::vector<EliminatedCandidate> eliminated;
    for (const auto rid : ids) {
        auto rec = get_record(rid);
        if (!rec.has_value()) continue;
        CandidateAssessment ca = evaluate_candidate(*rec, q);
        if (ca.in_scope) in_scope.push_back(std::move(ca));
        else eliminated.push_back({ca.record_id, ca.rejection, ca.rejection_note});
    }
    std::sort(in_scope.begin(), in_scope.end(), candidate_less);

    for (const auto& c : in_scope) {
        CandidateRanking cr;
        cr.record_id = c.record_id;
        cr.generation = c.generation;
        cr.best_placement = c.best_placement;
        const auto& order = factor_order();
        for (std::size_t i = 0; i < order.size(); ++i) {
            cr.factors.emplace_back(order[i], c.factors[i]);
            cr.factor_notes.push_back(c.factor_notes[i]);
        }
        cr.summary = "StateGeneration " + to_string(c.generation) + " candidate";
        result.ranked_candidates.push_back(std::move(cr));
    }
    result.eliminated = std::move(eliminated);

    // Determine outcome.
    if (in_scope.empty()) {
        bool saw_inval = false, saw_stale = false, saw_incompat = false, saw_insuff = false, saw_other = false;
        for (const auto& e : result.eliminated) {
            switch (e.reason) {
                case RejectionReason::INVALIDATED: case RejectionReason::TOMBSTONED: saw_inval = true; break;
                case RejectionReason::STALE_GENERATION: case RejectionReason::NOT_CURRENT:
                case RejectionReason::FRESHNESS_BELOW_REQUIREMENT: saw_stale = true; break;
                case RejectionReason::COMPATIBILITY_MISMATCH: case RejectionReason::DEPENDENCY_MISMATCH:
                case RejectionReason::MISSING_REQUIRED_DEPENDENCY: saw_incompat = true; break;
                case RejectionReason::INSUFFICIENT_EVIDENCE: saw_insuff = true; break;
                default: saw_other = true; break;
            }
        }
        // "only" outcomes require the single category to be present alone.
        if (ids.empty())
            result.outcome = QueryOutcome::NOT_FOUND;
        else if (saw_inval && !saw_stale && !saw_incompat && !saw_insuff && !saw_other)
            result.outcome = QueryOutcome::INVALIDATED_ONLY;
        else if (saw_incompat && !saw_stale && !saw_insuff && !saw_inval)
            result.outcome = QueryOutcome::INCOMPATIBLE_ONLY;
        else if (saw_stale && !saw_insuff && !saw_other)
            result.outcome = QueryOutcome::STALE_ONLY;
        else if (saw_insuff || saw_other)
            result.outcome = QueryOutcome::INSUFFICIENT_EVIDENCE;
        else
            result.outcome = QueryOutcome::NOT_FOUND;
    } else if (in_scope.size() == 1) {
        const auto& c = in_scope[0];
        result.selected_record_id = c.record_id;
        result.selected_generation = c.generation;
        result.selected_placement = c.best_placement;
        auto rec = get_record(c.record_id);
        if (rec.has_value()) {
            result.selected_state_id = rec->state_id;
            const bool exact = q.state_id.has_value() && !q.wildcard_state_id &&
                               rec->state_id == *q.state_id &&
                               (q.compatibility == CompatibilityRequirement::NONE ||
                                (rec->compatibility.has_value() && rec->compatibility->outcome == CompatibilityOutcome::EXACT)) &&
                               (q.content_digest.empty() || rec->content_digest == q.content_digest);
            result.outcome = exact ? QueryOutcome::FOUND_EXACT : QueryOutcome::FOUND_COMPATIBLE;
        }
    } else {
        const auto& c = in_scope[0];
        result.selected_record_id = c.record_id;
        result.selected_generation = c.generation;
        result.selected_placement = c.best_placement;
        result.outcome = QueryOutcome::FOUND_MULTIPLE;
        if (auto rec = get_record(c.record_id); rec.has_value()) result.selected_state_id = rec->state_id;
    }

    // Fill selected metadata.
    if (result.selected_record_id.has_value()) {
        auto rec = get_record(*result.selected_record_id);
        if (rec.has_value()) {
            result.freshness = rec->freshness;
            result.health = rec->health;
            result.compatibility_outcome = rec->compatibility ? std::string(to_string(rec->compatibility->outcome)) : "NONE";
            result.authority_boot = to_string(rec->authority_boot);
            result.semantic_digest = rec->semantic_digest;
            if (result.selected_placement.has_value()) {
                for (const auto& l : rec->locations) {
                    if (l.placement_id == *result.selected_placement) {
                        result.retrieval = l.retrieval;
                        result.provenance = l.provenance;
                        result.source = l.provenance == Evidence::SYNTHETIC ? "SYNTHETIC" : "PHYSICAL";
                        break;
                    }
                }
            }
            if (rec->provenance.has_value() && result.provenance == Evidence::UNKNOWN)
                result.provenance = rec->provenance->quality;
        }
    }

    if (historical) {
        result.notes.push_back("historical query: results are labelled non-current; they are not eligible for current ranking");
        if (!q.include_invalidated)
            result.notes.push_back("superseded and invalidated records excluded from this historical view");
    }

    // Summary.
    std::string sum = "outcome=" + std::string(to_string(result.outcome));
    if (result.selected_record_id.has_value())
        sum += " selected=" + to_string(*result.selected_record_id) + " gen=" + to_string(result.selected_generation.value_or(StateGeneration()));
    sum += " candidates=" + std::to_string(result.ranked_candidates.size());
    sum += " eliminated=" + std::to_string(result.eliminated.size());
    result.summary = sum;

    return result;
}


QueryResult StateIndexEngine::query(const QueryDescriptor& q) { std::lock_guard<std::mutex> lk(mtx_); return run_query(q, false); }

QueryResult StateIndexEngine::query_historical(const QueryDescriptor& q) { std::lock_guard<std::mutex> lk(mtx_); return run_query(q, true); }


void StateIndexEngine::verify_semantic_digest(const StateRecord& rec) const {
    const std::string computed = rec.compute_semantic_digest();
    if (computed != rec.semantic_digest)
        throw std::runtime_error("persistence: semantic digest mismatch for record " + to_string(rec.record_id));
}

void StateIndexEngine::save(const std::string& path) const {
    std::lock_guard<std::mutex> lk(mtx_);
    const std::vector<StateRecord> records = canonical_records();
    ByteWriter payload;
    payload.push_u64(records.size());
    for (const auto& rec : records) rec.serialize(payload);
    payload.push_u64(invalidations_.size());
    for (const auto& inv : invalidations_) inv.serialize(payload);
    payload.push_u64(tombstones_.size());
    for (const auto& [sid, t] : tombstones_) { (void)sid; t.serialize(payload); }
    authority_.epoch().serialize(payload);
    payload.push_u64(invalidation_seq_);
    accounting_.serialize(payload);

    const auto& p = payload.data();
    const std::uint32_t crc = crc32(p.data(), p.size());
    const std::array<std::uint8_t, 32> sha = sha256(p.data(), p.size());

    ByteWriter file;
    file.push_u32(kPersistMagic);
    file.push_u16(kPersistVersion);
    file.push_u16(0);
    file.push_bytes(p.data(), p.size());
    file.push_u32(crc);
    file.push_bytes(sha.data(), sha.size());

    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) throw std::runtime_error("persistence: cannot write " + tmp);
        const auto& out = file.data();
        f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
        f.flush();
        f.close();
    }
    // atomic replace: rename over an existing target on POSIX; on Windows
    // std::rename refuses to replace, so remove then rename.
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(path.c_str());
        if (std::rename(tmp.c_str(), path.c_str()) != 0)
            throw std::runtime_error("persistence: cannot rename temp to " + path);
    }
}

void StateIndexEngine::load(const std::string& path) {
    std::lock_guard<std::mutex> lk(mtx_);
    const std::vector<std::uint8_t> bytes = read_file(path);
    if (bytes.size() < 8u + 4u + 32u)
        throw std::runtime_error("persistence: file too short");
    ByteReader hdr(ByteSpan{bytes});
    const std::uint32_t magic = hdr.read_u32();
    if (magic != kPersistMagic) throw std::runtime_error("persistence: bad magic");
    const std::uint16_t version = hdr.read_u16();
    const std::uint16_t reserved = hdr.read_u16();
    (void)reserved;
    if (version != kPersistVersion) throw std::runtime_error("persistence: unsupported version");

    const std::size_t payload_start = hdr.position();
    if (payload_start > bytes.size() || bytes.size() - payload_start < 4u + 32u)
        throw std::runtime_error("persistence: payload truncated");
    const std::size_t payload_len = bytes.size() - payload_start - 4u - 32u;
    ByteReader tail(ByteSpan{bytes.data() + payload_start + payload_len, 4u + 32u});
    const std::uint32_t stored_crc = tail.read_u32();
    const ByteSpan stored_sha = tail.read_bytes(32);

    const std::uint32_t computed_crc = crc32(bytes.data() + payload_start, payload_len);
    if (computed_crc != stored_crc) throw std::runtime_error("persistence: CRC-32 mismatch");
    const std::array<std::uint8_t, 32> computed_sha = sha256(bytes.data() + payload_start, payload_len);
    if (!std::equal(computed_sha.begin(), computed_sha.end(), stored_sha.data))
        throw std::runtime_error("persistence: SHA-256 semantic digest mismatch");

    ByteReader pr(ByteSpan(bytes.data() + payload_start, payload_len));
    records_.clear();
    current_.clear();
    history_.clear();
    invalidations_.clear();
    tombstones_.clear();
    invalidation_seq_ = 0;
    accounting_ = Accounting();

    const std::uint64_t nrec = pr.read_u64();
    if (nrec > kBoundedRecord) throw std::runtime_error("persistence: record count exceeds bound");
    std::unordered_set<StateRecordId> seen_ids;
    std::unordered_map<StateId, std::unordered_set<std::uint64_t>> seen_gens;
    for (std::uint64_t i = 0; i < nrec; ++i) {
        StateRecord rec = StateRecord::deserialize(pr);
        if (!seen_ids.insert(rec.record_id).second)
            throw std::runtime_error("persistence: duplicate record id " + to_string(rec.record_id));
        if (!seen_gens[rec.state_id].insert(rec.state_generation.value()).second)
            throw std::runtime_error("persistence: duplicate or regressing generation for state " + to_string(rec.state_id));
        verify_semantic_digest(rec);
        records_[rec.record_id] = rec;
    }
    // Rebuild the per-state history map from the canonical records.
    history_.clear();
    for (const auto& [rid, rec] : records_) history_[rec.state_id].push_back(rid);

    const std::uint64_t ninv = pr.read_u64();
    if (ninv > kBoundedRecord) throw std::runtime_error("persistence: invalidation count exceeds bound");
    for (std::uint64_t i = 0; i < ninv; ++i) invalidations_.push_back(InvalidationRecord::deserialize(pr));

    const std::uint64_t ntom = pr.read_u64();
    if (ntom > kBoundedRecord) throw std::runtime_error("persistence: tombstone count exceeds bound");
    for (std::uint64_t i = 0; i < ntom; ++i) {
        TombstoneRecord t = TombstoneRecord::deserialize(pr);
        tombstones_[t.state_id] = t;
    }

    authority_.clear_live_workers();
    authority_.set_epoch(CoordinatorEpoch::deserialize(pr));
    invalidation_seq_ = pr.read_u64();
    accounting_ = Accounting::deserialize(pr);

    indexes_.rebuild(canonical_records());

    // Recovery: physical machine-local freshness must be re-validated. This
    // transition is idempotent: it only mutates locations that are not already
    // marked REVALIDATION_REQUIRED, so a second recovery makes no change.
    for (auto& [id, rec] : records_) {
        (void)id;
        bool changed = false;
        for (auto& l : rec.locations) {
            if (l.domain == MemoryDomain::HOST_MEMORY || l.domain == MemoryDomain::HOST_PINNED ||
                l.domain == MemoryDomain::CUDA_DEVICE) {
                if (l.freshness != Freshness::REVALIDATION_REQUIRED) {
                    l.freshness = Freshness::REVALIDATION_REQUIRED;
                    changed = true;
                }
            }
            if (l.process_handle.has_value()) {
                l.process_handle.reset();
                l.process_scope.reset();
                changed = true;
            }
        }
        if (changed) rec.semantic_digest = rec.compute_semantic_digest();
    }
    recompute_current();
}


std::vector<std::string> StateIndexEngine::index_verification() const {
    return indexes_.verify(canonical_records());
}

std::vector<std::string> StateIndexEngine::invariant_check() const {
    std::vector<std::string> errors;
    std::unordered_set<StateId> current_seen;
    for (const auto& [sid, rid] : current_) {
        if (!current_seen.insert(sid).second) {
            errors.push_back("duplicate current authority for state " + to_string(sid));
            continue;
        }
        auto rec = get_record(rid);
        if (!rec.has_value()) {
            errors.push_back("current record " + to_string(rid) + " does not exist");
            continue;
        }
        if (rec->state_id != sid) errors.push_back("current record belongs to a different state");
        if (rec->lifecycle == Lifecycle::INVALIDATED || rec->lifecycle == Lifecycle::TOMBSTONED ||
            rec->lifecycle == Lifecycle::SUPERSEDED)
            errors.push_back("current record " + to_string(rid) + " has terminal lifecycle " + to_string(rec->lifecycle));
        if (is_tombstoned(sid, rec->state_generation))
            errors.push_back("current record " + to_string(rid) + " is covered by a tombstone");
        if (policy.demand_usable_location_for_available && !rec->has_usable_location())
            errors.push_back("current record " + to_string(rid) + " for state " + to_string(sid) + " has no usable location");
        if (!dep_satisfied(*rec))
            errors.push_back("current record " + to_string(rid) + " has an unsatisfied required dependency");
    }

    // No two records of the same state may share a current generation.
    std::unordered_map<StateId, StateGeneration> max_gen;
    for (const auto& [id, rec] : records_) {
        (void)id;
        if (rec.lifecycle == Lifecycle::INVALIDATED || rec.lifecycle == Lifecycle::TOMBSTONED ||
            rec.lifecycle == Lifecycle::SUPERSEDED)
            continue;
        if (is_tombstoned(rec.state_id, rec.state_generation)) continue;
        auto it = max_gen.find(rec.state_id);
        if (it == max_gen.end() || rec.state_generation > it->second)
            max_gen[rec.state_id] = rec.state_generation;
    }
    for (const auto& [sid, rid] : current_) {
        auto it = max_gen.find(sid);
        auto rec = get_record(rid);
        if (it == max_gen.end() || !rec.has_value()) {
            errors.push_back("current " + to_string(rid) + " is not a max generation for state " + to_string(sid));
            continue;
        }
        if (rec->state_generation != it->second)
            errors.push_back("current " + to_string(rid) + " is not the highest eligible generation for state " + to_string(sid));
    }

    for (const auto& e : index_verification()) errors.push_back(e);

    for (const auto& [id, rec] : records_) {
        (void)id;
        const std::string computed = rec.compute_semantic_digest();
        if (computed != rec.semantic_digest)
            errors.push_back("semantic digest mismatch for record " + to_string(rec.record_id));
    }
    return errors;
}

std::string StateIndexEngine::explain_query(const QueryDescriptor& q, const QueryResult& r) const {
    std::string out;
    out += "Query " + to_string(q.query_id) + " (" + to_string(q.query_generation) + ") -> " + to_string(r.outcome) + "\n";
    out += r.summary + "\n";
    if (r.selected_record_id.has_value()) {
        out += "Selected " + to_string(*r.selected_record_id) + " gen " + to_string(r.selected_generation.value_or(StateGeneration())) +
               " at placement " + (r.selected_placement ? to_string(*r.selected_placement) : "none") + "\n";
        out += "authority boot " + r.authority_boot + " provenance " + to_string(r.provenance) + "\n";
    }
    for (std::size_t i = 0; i < r.ranked_candidates.size(); ++i) {
        const auto& c = r.ranked_candidates[i];
        out += "  rank[" + std::to_string(i) + "] " + to_string(c.record_id) + " gen " + to_string(c.generation) + ": ";
        for (const auto& [f, v] : c.factors) out += std::string(to_string(f)) + "=" + std::to_string(v) + " ";
        out += "\n";
    }
    for (const auto& e : r.eliminated)
        out += "  eliminated " + to_string(e.record_id) + ": " + to_string(e.reason) + " (" + e.note + ")\n";
    return out;
}

std::string StateIndexEngine::explain_candidate(const CandidateAssessment& c) const {
    std::string out = "StateGeneration " + to_string(c.generation) + " of record " + to_string(c.record_id);
    if (c.in_scope) {
        out += " selected because it is current, compatible, and its location is usable";
        for (std::size_t i = 0; i < c.factor_notes.size(); ++i)
            out += " [" + std::string(to_string(factor_order()[i])) + ": " + c.factor_notes[i] + "]";
    } else {
        out += " rejected: " + std::string(to_string(c.rejection)) + " (" + c.rejection_note + ")";
    }
    return out;
}

std::string StateIndexEngine::explain_rejection(RejectionReason r, const std::string& note) const {
    return "Candidate rejected: " + std::string(to_string(r)) + ". " + note;
}

std::string StateIndexEngine::explain_invalidation(const InvalidationRecord& inv) const {
    std::string out = "State invalidation for " + to_string(inv.state_id) + " through generation " + to_string(inv.bound_generation);
    out += " by boot " + to_string(inv.worker_boot) + " at epoch " + to_string(inv.coordinator_epoch);
    out += " reason: " + inv.reason;
    return out;
}

std::string StateIndexEngine::explain_tombstone(const TombstoneRecord& t) const {
    std::string out = "State tombstone " + to_string(t.tombstone_id) + " for " + to_string(t.state_id);
    out += " floor generation " + to_string(t.floor_generation) + " by boot " + to_string(t.worker_boot);
    out += " reason: " + t.reason;
    return out;
}

std::string StateIndexEngine::explain_supersession(const StateRecord& current, const StateRecord& previous) const {
    return "State " + to_string(current.state_id) + " generation " + to_string(current.state_generation) +
           " supersedes generation " + to_string(previous.state_generation) +
           " (" + to_string(previous.record_id) + " retained as historical)";
}

std::string StateIndexEngine::explain_recovery(const std::string& note) const {
    return "Recovered: " + note +
           ". Live worker authority cleared; physical locations require revalidation.";
}

std::string StateIndexEngine::explain_location(const StateLocation& loc) const {
    std::string out = "Location " + to_string(loc.placement_id) + " node " + to_string(loc.node_id);
    if (loc.device_id) out += " device " + to_string(loc.device_id);
    out += " domain " + std::string(to_string(loc.domain)) + " health " + to_string(loc.health);
    out += " freshness " + to_string(loc.freshness) + " provenance " + to_string(loc.provenance);
    return out;
}

}  // namespace stateindex