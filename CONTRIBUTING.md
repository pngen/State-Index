# Contributing to State Index

Thank you for your interest in contributing to State Index. This document
describes the conventions we use so that every contribution lands cleanly.

## Scope

State Index is the lookup/index substrate for reusable AI state. It answers:

* What reusable state matches a requirement, and where is it?
* Which copy/generation is authoritative, and what evidence justifies it?

State Index deliberately does **not** become a Distributed Cache Directory, a
Runtime Registry, a Compatibility Registry, a State Provenance system, a
Storage Fabric, or a Checkpoint Store. Contributions that pull those
boundaries into State Index will be redirected.

## Development setup

State Index targets:

* C++20
* Windows / MSVC (19.44) with CMake and Ninja
* A local filesystem is the default storage substrate
* CUDA is optional and must remain off by default

Clone the repository, then:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="/W4 /WX"
cmake --build build
ctest --test-dir build --output-on-failure
```

## Coding guidelines

* Use the `stateindex` namespace.
* Use strong types for identities and generations. Do not pass raw integers
  where a `StateId` or `StateGeneration` exists.
* Keep behavior deterministic. Fix any seed and print it.
* All indexing and ranking must be reproducible and free of hidden global
  scores.
* Prefer narrow locks. Never perform blocking socket I/O while holding a
  global index lock.
* Guard every lifecycle transition and every mutation against stale authority:
  a stale mutation must never change a current result.
* The project compiles under MSVC `/W4 /WX`. New code must be warning-free.

## Testing

* Unit tests live under `tests/` and are registered as CTest targets named
  `test_*`.
* Property tests use fixed seeds and report the seed on failure.
* Adversarial and concurrency coverage is required for authority, index
  consistency, and persistence.

## Commit messages

Commit messages are neutral and public-facing. Use ordinary engineering
subjects, for example:

* `feat: implement indexed state lookup`
* `fix: reject stale index authority`
* `test: add restart and invalidation coverage`
* `docs: refine README`

Do not include internal workflow, agent, harness, or test-constraint
directives in commit subjects or bodies.

## License

By contributing, you agree that your contributions are licensed under the
Apache License 2.0. Copyright 2026 Summon Software Labs.
