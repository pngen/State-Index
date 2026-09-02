// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "test_util.hpp"
#include "stateindex/stateindex.hpp"
#include "stateindex/protocol.hpp"

using namespace stateindex;

static void frame_roundtrip() {
    ByteWriter p;
    p.push_u32(123456);
    p.push_string("hello");
    auto frame = encode_frame(MsgKind::REGISTER_STATE, p.data());
    auto [f, err] = decode_frame(ByteSpan(frame));
    CHECK(err == FrameError::NONE);
    CHECK(f.kind == MsgKind::REGISTER_STATE);
    ByteReader r(ByteSpan(f.payload));
    CHECK(r.read_u32() == 123456);
    CHECK(r.read_string() == "hello");
    CHECK(r.remaining() == 0);
}

static void frame_rejections() {
    ByteWriter p;
    p.push_u32(1);
    std::vector<std::uint8_t> frame = encode_frame(MsgKind::PING, p.data());

    // Bad magic.
    { auto b = frame; b[0] ^= 0xff; auto [f, e] = decode_frame(ByteSpan(b)); CHECK(e == FrameError::BAD_MAGIC); }
    // Bad version.
    { auto b = frame; b[4] ^= 0xff; auto [f, e] = decode_frame(ByteSpan(b)); CHECK(e == FrameError::BAD_VERSION); }
    // Invalid kind (with a re-validated CRC so the kind check fires).
    {
        auto b = frame;
        b[6] = 0xff; b[7] = 0xff;
        const std::uint32_t c = crc32_over_two(b.data(), 16, b.data() + 16 + 8, b.size() - 24);
        b[16] = static_cast<std::uint8_t>(c & 0xffu);
        b[17] = static_cast<std::uint8_t>((c >> 8) & 0xffu);
        b[18] = static_cast<std::uint8_t>((c >> 16) & 0xffu);
        b[19] = static_cast<std::uint8_t>((c >> 24) & 0xffu);
        auto [f, e] = decode_frame(ByteSpan(b));
        CHECK(e == FrameError::INVALID_KIND);
    }
    // Truncation.
    { std::vector<std::uint8_t> t(frame.begin(), frame.begin() + 10); auto [f, e] = decode_frame(ByteSpan(t)); CHECK(e == FrameError::TRUNCATED); }
    // Bad CRC.
    { auto b = frame; b[b.size() - 1] ^= 0xff; auto [f, e] = decode_frame(ByteSpan(b)); CHECK(e == FrameError::BAD_CRC); }
    // Trailing garbage.
    { auto b = frame; b.push_back(0x42); auto [f, e] = decode_frame(ByteSpan(b)); CHECK(e == FrameError::TRAILING_GARBAGE); }
    // Oversized: craft a header declaring a length above the frame cap.
    {
        ByteWriter h;
        h.push_u32(kProtocolMagic); h.push_u16(kProtocolVersion); h.push_u16(3);
        h.push_u64(0xFFFFFFFFu); h.push_u32(0); h.push_u32(0);
        auto b = h.data();
        auto [f, e] = decode_frame(ByteSpan(b));
        CHECK(e == FrameError::OVERSIZED);
    }
}

static void query_descriptor_serialize() {
    QueryDescriptor q;
    q.query_id = QueryId(7);
    q.kind = StateKind::TENSOR_STATE;
    q.state_id = StateId(42);
    q.compatibility = CompatibilityRequirement::COMPATIBLE;
    q.dependency_requirements.push_back(DependencyRequirement{StateId(99), StateGeneration(3), true});
    q.max_retrieval_cost = 12.5;
    q.locality_preference = {MemoryDomain::HOST_MEMORY, MemoryDomain::CUDA_DEVICE};
    q.required_health = Health::HEALTHY;
    ByteWriter w; q.serialize(w);
    ByteReader r(ByteSpan(w.data()));
    QueryDescriptor q2 = QueryDescriptor::deserialize(r);
    CHECK(q2.query_id == q.query_id);
    CHECK(q2.kind == StateKind::TENSOR_STATE);
    CHECK(q2.state_id == StateId(42));
    CHECK(q2.compatibility == CompatibilityRequirement::COMPATIBLE);
    CHECK(q2.dependency_requirements.size() == 1);
    CHECK(q2.dependency_requirements[0].target_state == StateId(99));
    CHECK(q2.max_retrieval_cost == 12.5);
    CHECK(q2.locality_preference.size() == 2);
    CHECK(r.remaining() == 0);
}

int main() {
    int f = 0;
    f += sittest::run("frame_roundtrip", frame_roundtrip);
    f += sittest::run("frame_rejections", frame_rejections);
    f += sittest::run("query_descriptor_serialize", query_descriptor_serialize);
    if (f == 0) std::cout << "ALL PASS\n";
    return f == 0 ? 0 : 1;
}
