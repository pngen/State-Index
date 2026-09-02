# State Index

State Index is a production-quality, vendor-neutral C++20 runtime that makes
reusable AI state discoverable as a first-class systems concept.

## The systems question

State Index owns one systems boundary:

> How should reusable AI state be indexed so the system can determine what
> exists, where it is, which generation is current, what it is compatible
> with, what depends on it, how fresh and authoritative it is, and which
> candidate is cheapest and safest to retrieve or reuse?

Modern AI infrastructure accumulates KV state, tensors, prefixes, checkpoints,
model artifacts, compiled kernels, execution graphs, adapters, plans,
manifests, and other reusable machine-produced state across processes, hosts,
storage tiers, accelerators, caches, and generations. A generic key-value
lookup is not enough. State Index makes identity, generation, location,
compatibility, provenance, dependencies, authority, freshness, and retrieval
economics explicit.

## What State Index is not

State Index is the lookup/index substrate. It is deliberately not any of the
following, which remain separate systems:

* **Distributed Cache Directory** — State Index does not move or manage data.
* **Runtime Registry** — State Index does not own live service discovery.
* **Compatibility Registry** — State Index references compatibility semantics
  rather than replacing that runtime.
* **State Provenance** — State Index stores references and summaries, not the
  full derivation DAG.
* **Storage Fabric** — State Index does not allocate or back storage.
* **Checkpoint Store** — State Index does not persist model bytes.

## State kinds

Reusable state kinds include `KV_STATE`, `PREFIX_STATE`, `TENSOR_STATE`,
`MODEL_ARTIFACT`, `MODEL_SHARD`, `ADAPTER`, `COMPILED_KERNEL`,
`EXECUTION_GRAPH`, `EXECUTION_PLAN`, `CHECKPOINT`,
`CHECKPOINT_CHUNK`, `CHECKPOINT_MANIFEST`, `STORAGE_OBJECT`, `BUFFER`,
`RUNTIME_ARTIFACT`, and `GENERIC_STATE`. The enum is designed for
extension. State is never collapsed into an untyped blob.

## Identities and generations

State Index uses strong, non-interchangeable identities (`StateId`,
`RecordId`, `NamespaceId`, `OwnerId`, `ProducerId`, `NodeId`,
`DeviceId`, ...) and strong generation types (`StateGeneration`,
`RecordGeneration`, `CoordinatorEpoch`, `WorkerBootId`,
`LocationGeneration`, ...). A larger generation from an old `WorkerBootId`
never fences a fresh process incarnation; authority is incarnation-scoped.

## State record

A `StateRecord` carries its identity, state generation, state kind,
namespace, owner, producer, logical size, content digest, references, and
locations. State identity and physical location are separate: one `StateId`
may have several replicas, placements, and locations, but exactly one current
authoritative generation per authority domain unless policy permits otherwise.

## Lifecycle

Records move through `DISCOVERED`, `AVAILABLE`, `DEGRADED`, `STALE`,
`INVALIDATED`, `SUPERSEDED`, `TOMBSTONED`, `RETIRED`, `MISSING`,
and `FAILED`. Transitions are guarded. A record cannot become `AVAILABLE`
merely because an index entry exists: availability requires current authority
and at least one usable location.

## Secondary indexes

State Index maintains explicit, typed secondary indexes — by `StateId`,
state kind, namespace, owner, producer, content digest, compatibility
identity, dependency, node, device, memory/storage domain, freshness, health,
lifecycle, generation, locality class, and size range. It avoids one giant
opaque composite hash.

## Query model

A `QueryDescriptor` requests a state kind, identity, generation rules,
compatibility and dependency requirements, locations, freshness, health,
retrieval cost, locality, and durability. Queries are exact or predicate.
Outcomes are `FOUND_EXACT`, `FOUND_COMPATIBLE`, `FOUND_MULTIPLE`,
`NOT_FOUND`, `STALE_ONLY`, `INCOMPATIBLE_ONLY`,
`INVALIDATED_ONLY`, `INSUFFICIENT_EVIDENCE`, and `UNKNOWN`.
`UNKNOWN` and `INSUFFICIENT_EVIDENCE` never silently become
`FOUND_COMPATIBLE`.

## Ranking

The planner eliminates candidates first using hard filters, then ranks the
survivors by named factors such as exact identity match, generation recency,
compatibility quality, locality, latency, bandwidth, transfer bytes, restore
cost, reuse cost, health, replica count, freshness, and policy preference.
There is no single opaque master score, and tie-breaking is deterministic.

## Locations

A `StateLocation` names a node, optional device, memory/storage domain,
placement, replica, generation, address/key abstraction, byte size, locality,
access class, health, freshness, provenance, retrieval estimate, and
authority. Supported domains include `CUDA_DEVICE`, `HOST_PINNED`,
`HOST_MEMORY`, `LOCAL_FILESYSTEM`, `LOCAL_NVME_CLASS`,
`SHARED_FILESYSTEM_CLASS`, `OBJECT_STORAGE_CLASS`,
`REMOTE_CACHE_CLASS`, `SYNTHETIC_REMOTE`, and `UNKNOWN`. A location is
only called `LOCAL_NVME_CLASS` when proven. Raw pointers are never persisted
as cross-process authority.

## Compatibility, provenance, and dependency references

State Index stores references and summaries rather than owning those systems.
It supports exact compatibility identity references, compatibility
generation and freshness, and optional callbacks. Provenance references carry
a `ProvenanceId`, producer, source generation, derivation digest, quality,
and authority. Dependency references carry a dependency `StateId`, required
generation, dependency generation, kind, and required/optional semantics.
A record whose required dependency is invalidated or stale becomes ineligible
per policy; State Index supports dependency-driven invalidation but is not a
Dependency Fabric.

## Invalidation and supersession

Invalidation is central. State Index supports invalidation by state,
generation, owner, producer, location, compatibility, dependency, backend,
explicit tombstone, and policy. Invalidation does not rewrite history. A stale
invalidation never invalidates fresher state, and a stale re-add never
resurrects invalidated authority. Publishing generation N+1 supersedes N for
current lookup while preserving historical access; history is never physically
deleted as part of ordinary supersession.

## Tombstones

A tombstone binds a `StateId`, minimum/current generation, `TombstoneId`,
authority generation, `CoordinatorEpoch`, source `WorkerBootId`, reason,
timestamp, and provenance. A stale producer cannot republish generation at or
below the tombstoned generation as current.

## Historical query

Historical lookup supports exact generation, generation range, as-of index
generation, and a flag to include invalidated or superseded records. Historical
results are clearly labelled non-current and never enter ordinary current
candidate ranking.

## Distributed update authority

State Index is logically centralizable or sharded. Version 1.0.0 proves
distributed update authority with a coordinator and worker/source processes
over framed TCP. Every mutation carries its `CoordinatorEpoch`,
`WorkerBootId`, state/record generation, relevant location and dependency
generations, and attempt/dispatch generation. Stale mutations are rejected
before any state change.

## Persistence and recovery

Persistence uses versioned deterministic binary format with a magic header,
version, bounded counts, CRC-32, and a SHA-256 semantic digest. It persists
logical records, generations, source data for secondary indexes, locations,
replicas, references, dependencies, invalidations, tombstones, history,
policy, accounting, and completed observations. Internal bucket layout is
never persisted. Secondary indexes are rebuilt deterministically from
canonical source records on recovery. Writes are atomic: temp, flush, close,
rename. On recovery, live `WorkerBootId` authority is cleared and
process-local memory and `CUDA_DEVICE` locations become
`REVALIDATION_REQUIRED`.

## Real local state proof

State Index is validated against actual local state records backed by
memory, the local filesystem, and optional CUDA device locations. The
validation creates records of multiple kinds and generations, indexes
multiple locations, queries by identity, kind, node, and location, invalidates
locations, supersedes generations, tombstones old and current generations,
runs historical and persistence/recovery queries, and demonstrates that query
results change as authority and freshness change.

## CUDA proof

When enabled, State Index validates a real CUDA device buffer as an indexed
`CUDA_DEVICE` location: it allocates a deterministic buffer, indexes it,
runs a real kernel to establish content, verifies CPU parity, queries the
device, frees and invalidates the physical location, rejects stale location
replay, re-registers a fresh buffer under a new generation, and queries again.
Raw CUDA pointers are never persisted as cross-process authority.

## Synthetic distributed scenarios

State Index includes deterministic synthetic multi-node/location scenarios
for physical infrastructure that is not available. Every synthetic fact is
explicitly labelled `provenance = SYNTHETIC`.

## CLI

The `state-index` command provides `add`, `show`, `query`,
`query-kind`, `locations`, `supersede`, `invalidate`, `tombstone`,
`history`, `dependencies`, `explain`, `simulate`, `save`,
`recover`, and `benchmark`. Output exposes `StateId`, generation, kind,
location, compatibility, freshness, health, authority, provenance, outcome,
selected candidate, and elimination reasons.

## Examples

The `examples/` directory contains runnable programs that call the actual
library APIs, covering identity, exact and kind lookup, multi-location,
generation supersession, compatibility and dependency filtering, locality
ranking, invalidation, tombstones, historical query, persistence/recovery,
multiprocess authority, CUDA location, and query explanation.

## Benchmarks

The `benchmarks/` target reports completed-work throughput (`ops/s`,
`ns/op`, record counts, candidate counts, thread counts, wall time) for
canonical lookup, indexing, update, supersession, invalidation, persistence,
protocol encode/decode, and mixed read/write workloads at 1k, 10k, and 100k
records where practical.

## Downstream package consumption

State Index installs a `StateIndexConfig.cmake` that supports
`find_package(StateIndex CONFIG REQUIRED)` and links the imported target
`StateIndex::stateindex`. A downstream consumer can create state records,
locations, query exact and compatible/local candidates, supersede, invalidate,
tombstone, run history, and print explanations without touching the build tree.

## Limitations

* Local and process state are physically validated.
* CUDA location is physically validated when the CUDA proof is enabled.
* Remote and distributed locations are synthetic where physical
  infrastructure is unavailable and are labelled `SYNTHETIC`.
* State Index references compatibility and provenance semantics rather than
  replacing those runtimes.
* Physical location freshness may require revalidation after restart.
* Unknown facts remain `UNKNOWN`.

## License
Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.