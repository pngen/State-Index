// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef STATEINDEX_LIMITS_HPP
#define STATEINDEX_LIMITS_HPP

#include <cstddef>
#include <cstdint>

namespace stateindex {

// Hard bounds used across the runtime for bounded parsing and allocation.
// These prevent a corrupt or adversarial stream from causing unbounded work.
inline constexpr std::uint64_t kMaxRecordCount = 1000000;
inline constexpr std::uint64_t kMaxLocationsPerRecord = 4096;
inline constexpr std::uint64_t kMaxDependenciesPerRecord = 4096;
inline constexpr std::uint64_t kMaxStrings = 1u << 24;       // 16 MiB max string length
inline constexpr std::size_t kMaxRecordSerializedBytes = 1u << 26;   // 64 MiB
inline constexpr std::size_t kMaxFrameBytes = 1u << 26;             // 64 MiB frame
inline constexpr std::size_t kProtocolHeaderSize = 24;              // magic+version+kind+len+crc
inline constexpr std::uint32_t kProtocolMagic = 0x53584944u;        // "SXID"
inline constexpr std::uint16_t kProtocolVersion = 1;

}  // namespace stateindex

#endif  // STATEINDEX_LIMITS_HPP
