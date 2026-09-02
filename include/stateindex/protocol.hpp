// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef STATEINDEX_PROTOCOL_HPP
#define STATEINDEX_PROTOCOL_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "stateindex/bytebuf.hpp"
#include "stateindex/digest.hpp"
#include "stateindex/limits.hpp"
#include "stateindex/strong.hpp"

namespace stateindex {

enum class MsgKind : std::uint16_t {
    HELLO = 1, HELLO_ACK = 2, REGISTER_STATE = 3, ADD_LOCATION = 4, REMOVE_LOCATION = 5,
    INVALIDATE = 6, TOMBSTONE = 7, QUERY = 8, QUERY_RESULT = 9, MUTATION_RESULT = 10,
    SAVE = 11, LOAD = 12, PING = 13, PONG = 14, TERMINATE = 15, ACK = 16,
};

inline const char* to_string(MsgKind k) {
    switch (k) {
        case MsgKind::HELLO: return "HELLO";
        case MsgKind::HELLO_ACK: return "HELLO_ACK";
        case MsgKind::REGISTER_STATE: return "REGISTER_STATE";
        case MsgKind::ADD_LOCATION: return "ADD_LOCATION";
        case MsgKind::REMOVE_LOCATION: return "REMOVE_LOCATION";
        case MsgKind::INVALIDATE: return "INVALIDATE";
        case MsgKind::TOMBSTONE: return "TOMBSTONE";
        case MsgKind::QUERY: return "QUERY";
        case MsgKind::QUERY_RESULT: return "QUERY_RESULT";
        case MsgKind::MUTATION_RESULT: return "MUTATION_RESULT";
        case MsgKind::SAVE: return "SAVE";
        case MsgKind::LOAD: return "LOAD";
        case MsgKind::PING: return "PING";
        case MsgKind::PONG: return "PONG";
        case MsgKind::TERMINATE: return "TERMINATE";
        case MsgKind::ACK: return "ACK";
    }
    return "UNKNOWN";
}

struct Frame {
    MsgKind kind = MsgKind::PING;
    std::vector<std::uint8_t> payload;
};

enum class FrameError { NONE, BAD_MAGIC, BAD_VERSION, OVERSIZED, TRUNCATED, BAD_CRC, INVALID_KIND, TRAILING_GARBAGE };

inline const char* to_string(FrameError e) {
    switch (e) {
        case FrameError::NONE: return "NONE";
        case FrameError::BAD_MAGIC: return "BAD_MAGIC";
        case FrameError::BAD_VERSION: return "BAD_VERSION";
        case FrameError::OVERSIZED: return "OVERSIZED";
        case FrameError::TRUNCATED: return "TRUNCATED";
        case FrameError::BAD_CRC: return "BAD_CRC";
        case FrameError::INVALID_KIND: return "INVALID_KIND";
        case FrameError::TRAILING_GARBAGE: return "TRAILING_GARBAGE";
    }
    return "UNKNOWN";
}

inline std::uint32_t crc32_over_two(const std::uint8_t* a, std::size_t an,
                                    const std::uint8_t* b, std::size_t bn) {
    static const std::vector<std::uint32_t> table = [] {
        const std::uint32_t kPoly = 0xEDB88320u;
        std::vector<std::uint32_t> t(256);
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? (kPoly ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < an; ++i) crc = table[(crc ^ a[i]) & 0xffu] ^ (crc >> 8);
    for (std::size_t i = 0; i < bn; ++i) crc = table[(crc ^ b[i]) & 0xffu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// Encode a frame: header [magic|version|kind|len] (16) + crc (4) + reserved (4)
// + payload. CRC is over the 16-byte header prefix plus the payload.
inline std::vector<std::uint8_t> encode_frame(MsgKind kind, const std::uint8_t* payload, std::size_t len) {
    ByteWriter hdr;
    hdr.push_u32(kProtocolMagic);
    hdr.push_u16(kProtocolVersion);
    hdr.push_u16(static_cast<std::uint16_t>(kind));
    hdr.push_u64(static_cast<std::uint64_t>(len));
    const std::uint32_t crc = crc32_over_two(hdr.data().data(), hdr.size(), payload, len);
    ByteWriter f;
    f.push_bytes(hdr.data().data(), hdr.size());
    f.push_u32(crc);
    f.push_u32(0);
    if (len > 0) f.push_bytes(payload, len);
    return f.data();
}

inline std::vector<std::uint8_t> encode_frame(MsgKind kind, const std::vector<std::uint8_t>& payload) {
    return encode_frame(kind, payload.data(), payload.size());
}

// Decode a complete frame. Returns {frame, error}; on error the frame is empty.
inline std::pair<Frame, FrameError> decode_frame(ByteSpan bytes) {
    if (bytes.size < kProtocolHeaderSize)
        return {Frame{}, FrameError::TRUNCATED};
    ByteReader r(bytes);
    const std::uint32_t magic = r.read_u32();
    if (magic != kProtocolMagic) return {Frame{}, FrameError::BAD_MAGIC};
    const std::uint16_t version = r.read_u16();
    if (version != kProtocolVersion) return {Frame{}, FrameError::BAD_VERSION};
    const std::uint16_t kind_raw = r.read_u16();
    const std::uint64_t len = r.read_u64();
    const std::uint32_t stored_crc = r.read_u32();
    const std::uint32_t reserved = r.read_u32();
    (void)reserved;
    if (len > kMaxFrameBytes) return {Frame{}, FrameError::OVERSIZED};
    if (bytes.size - kProtocolHeaderSize < static_cast<std::size_t>(len))
        return {Frame{}, FrameError::TRUNCATED};
    if (bytes.size - kProtocolHeaderSize > static_cast<std::size_t>(len))
        return {Frame{}, FrameError::TRAILING_GARBAGE};
    const std::uint32_t computed = crc32_over_two(bytes.data, 16, bytes.data + 16 + 8, static_cast<std::size_t>(len));
    if (computed != stored_crc) return {Frame{}, FrameError::BAD_CRC};
    const int kind_int = static_cast<int>(kind_raw);
    if (kind_int < static_cast<int>(MsgKind::HELLO) || kind_int > static_cast<int>(MsgKind::ACK))
        return {Frame{}, FrameError::INVALID_KIND};
    Frame f;
    f.kind = static_cast<MsgKind>(kind_raw);
    const std::uint8_t* p = bytes.data + 16 + 8;
    f.payload.assign(p, p + static_cast<std::size_t>(len));
    return {std::move(f), FrameError::NONE};
}

}  // namespace stateindex

#endif  // STATEINDEX_PROTOCOL_HPP
