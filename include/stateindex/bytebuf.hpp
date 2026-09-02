// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef STATEINDEX_BYTEBUF_HPP
#define STATEINDEX_BYTEBUF_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>

namespace stateindex {

// Represents a bounded range over a byte buffer. Used for read-through of a
// serialized image (persistence record or protocol frame).
struct ByteSpan {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;

    ByteSpan() = default;
    ByteSpan(const std::uint8_t* d, std::size_t s) : data(d), size(s) {}
    explicit ByteSpan(const std::vector<std::uint8_t>& v) : data(v.data()), size(v.size()) {}

    [[nodiscard]] bool empty() const noexcept { return size == 0; }
};

// Growing, bounded serializer. All writes append to an internal vector.
class ByteWriter {
public:
    ByteWriter() = default;
    explicit ByteWriter(std::size_t reserve) { buf_.reserve(reserve); }

    void push_byte(std::uint8_t b) { buf_.push_back(b); }
    void push_u16(std::uint16_t v) {
        buf_.push_back(static_cast<std::uint8_t>(v & 0xffu));
        buf_.push_back(static_cast<std::uint8_t>((v >> 8) & 0xffu));
    }
    void push_u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i)
            buf_.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xffu));
    }
    void push_u64(std::uint64_t v) {
        for (int i = 0; i < 8; ++i)
            buf_.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xffu));
    }
    void push_i64(std::int64_t v) {
        push_u64(static_cast<std::uint64_t>(v));
    }
    void push_bytes(const std::uint8_t* p, std::size_t n) {
        if (n > 0) buf_.insert(buf_.end(), p, p + n);
    }
    void push_span(ByteSpan s) { push_bytes(s.data, s.size); }
    void push_string(const std::string& s) {
        push_u64(s.size());
        push_bytes(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
    }
    void push_cstr(const char* s) { push_string(s == nullptr ? std::string() : std::string(s)); }

    [[nodiscard]] const std::vector<std::uint8_t>& data() const noexcept { return buf_; }
    [[nodiscard]] std::vector<std::uint8_t>& data() noexcept { return buf_; }
    [[nodiscard]] std::size_t size() const noexcept { return buf_.size(); }
    void clear() { buf_.clear(); }
    void reserve(std::size_t n) { buf_.reserve(n); }

private:
    std::vector<std::uint8_t> buf_;
};

// Bounded, checking reader. It does not own the bytes it reads.
class ByteReader {
public:
    explicit ByteReader(ByteSpan span) : span_(span) {}

    [[nodiscard]] std::size_t remaining() const noexcept { return span_.size - pos_; }
    [[nodiscard]] std::size_t position() const noexcept { return pos_; }
    [[nodiscard]] bool has(std::size_t n) const noexcept { return remaining() >= n; }

    std::uint8_t read_u8() {
        require(1);
        return span_.data[pos_++];
    }
    std::uint16_t read_u16() {
        require(2);
        std::uint16_t v = span_.data[pos_] | (static_cast<std::uint16_t>(span_.data[pos_ + 1]) << 8);
        pos_ += 2;
        return v;
    }
    std::uint32_t read_u32() {
        require(4);
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(span_.data[pos_ + i]) << (8 * i);
        pos_ += 4;
        return v;
    }
    std::uint64_t read_u64() {
        require(8);
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(span_.data[pos_ + i]) << (8 * i);
        pos_ += 8;
        return v;
    }
    std::int64_t read_i64() { return static_cast<std::int64_t>(read_u64()); }
    ByteSpan read_bytes(std::size_t n) {
        require(n);
        ByteSpan out(span_.data + pos_, n);
        pos_ += n;
        return out;
    }
    std::string read_string() {
        std::uint64_t n = read_u64();
        if (n > remaining()) throw std::runtime_error("byte reader: string length exceeds remaining bytes");
        require(static_cast<std::size_t>(n));
        std::string s(reinterpret_cast<const char*>(span_.data + pos_), static_cast<std::size_t>(n));
        pos_ += static_cast<std::size_t>(n);
        return s;
    }
    void skip(std::size_t n) { require(n); pos_ += n; }

private:
    ByteSpan span_;
    std::size_t pos_ = 0;

    void require(std::size_t n) const {
        if (remaining() < n) throw std::runtime_error("byte reader: out of bounds");
    }
};

}  // namespace stateindex

#endif  // STATEINDEX_BYTEBUF_HPP
