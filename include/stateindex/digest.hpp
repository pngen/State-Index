// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef STATEINDEX_DIGEST_HPP
#define STATEINDEX_DIGEST_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace stateindex {

// CRC-32 (IEEE 802.3, polynomial 0xEDB88320). Deterministic and dependency free.
inline std::uint32_t crc32(const std::uint8_t* data, std::size_t len) noexcept {
    static constexpr std::uint32_t kPoly = 0xEDB88320u;
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? (kPoly ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i)
        crc = table[(crc ^ data[i]) & 0xffu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

inline std::uint32_t crc32(std::span<const std::uint8_t> data) noexcept {
    return crc32(data.data(), data.size());
}

// SHA-256 (FIPS 180-4). Returns the 32-byte digest.
inline std::array<std::uint8_t, 32> sha256(const std::uint8_t* data, std::size_t len) {
    static constexpr std::array<std::uint32_t, 64> k = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
        0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
        0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
        0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
        0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
        0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    };
    std::uint32_t h[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    // Message padding.
    std::vector<std::uint8_t> msg(data, data + len);
    const std::uint64_t bit_len = static_cast<std::uint64_t>(len) * 8u;
    msg.push_back(0x80u);
    while ((msg.size() % 64u) != 56u) msg.push_back(0x00u);
    for (int i = 7; i >= 0; --i)
        msg.push_back(static_cast<std::uint8_t>((bit_len >> (8 * i)) & 0xffu));

    auto rotr = [](std::uint32_t x, int n) -> std::uint32_t { return (x >> n) | (x << (32 - n)); };

    std::uint32_t w[64];
    for (std::size_t block = 0; block < msg.size(); block += 64) {
        for (int i = 0; i < 16; ++i) {
            std::size_t off = block + static_cast<std::size_t>(i) * 4u;
            w[i] = (static_cast<std::uint32_t>(msg[off]) << 24) |
                   (static_cast<std::uint32_t>(msg[off + 1]) << 16) |
                   (static_cast<std::uint32_t>(msg[off + 2]) << 8) |
                   static_cast<std::uint32_t>(msg[off + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            std::uint32_t ch = (e & f) ^ ((~e) & g);
            std::uint32_t t1 = hh + s1 + ch + k[i] + w[i];
            std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            std::uint32_t t2 = s0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    std::array<std::uint8_t, 32> out{};
    for (int i = 0; i < 8; ++i) {
        out[static_cast<std::size_t>(i) * 4] = static_cast<std::uint8_t>((h[i] >> 24) & 0xffu);
        out[static_cast<std::size_t>(i) * 4 + 1] = static_cast<std::uint8_t>((h[i] >> 16) & 0xffu);
        out[static_cast<std::size_t>(i) * 4 + 2] = static_cast<std::uint8_t>((h[i] >> 8) & 0xffu);
        out[static_cast<std::size_t>(i) * 4 + 3] = static_cast<std::uint8_t>(h[i] & 0xffu);
    }
    return out;
}

inline std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> data) {
    return sha256(data.data(), data.size());
}

inline std::string to_hex(std::span<const std::uint8_t> bytes) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (std::uint8_t b : bytes) {
        out.push_back(digits[(b >> 4) & 0xf]);
        out.push_back(digits[b & 0xf]);
    }
    return out;
}

inline std::string sha256_hex(const std::uint8_t* data, std::size_t len) {
    return to_hex(sha256(data, len));
}

}  // namespace stateindex

#endif  // STATEINDEX_DIGEST_HPP
