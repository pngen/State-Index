// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef STATEINDEX_TCP_HPP
#define STATEINDEX_TCP_HPP

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "stateindex/protocol.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace stateindex {

using tcp_handle = std::intptr_t;

inline void tcp_init() {
#ifdef _WIN32
    WSADATA d;
    if (WSAStartup(MAKEWORD(2, 2), &d) != 0) throw std::runtime_error("WSAStartup failed");
#endif
}
inline void tcp_shutdown() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// A server socket bound to the loopback port. Accepts one connection at a time.
class TcpServer {
public:
    explicit TcpServer(int port) {
        tcp_init();
#ifdef _WIN32
        server_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
        server_ = ::socket(AF_INET, SOCK_STREAM, 0);
#endif
        if (server_ < 0) throw std::runtime_error("socket failed");
        int opt = 1;
        setsockopt(static_cast<int>(server_), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(static_cast<unsigned short>(port));
        if (::bind(static_cast<int>(server_), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            close_();
            throw std::runtime_error("bind failed (port in use?)");
        }
        if (::listen(static_cast<int>(server_), 8) != 0) {
            close_();
            throw std::runtime_error("listen failed");
        }
    }
    ~TcpServer() { close_(); }

    // Accept a client connection. Returns the client handle (caller must close).
    tcp_handle accept() {
#ifdef _WIN32
        SOCKET c = ::accept(static_cast<SOCKET>(server_), nullptr, nullptr);
        if (c == INVALID_SOCKET) throw std::runtime_error("accept failed");
        return static_cast<tcp_handle>(c);
#else
        int c = ::accept(static_cast<int>(server_), nullptr, nullptr);
        if (c < 0) throw std::runtime_error("accept failed");
        return static_cast<tcp_handle>(c);
#endif
    }

private:
    std::intptr_t server_ = -1;
    void close_() {
#ifdef _WIN32
        if (server_ >= 0) { ::closesocket(static_cast<SOCKET>(server_)); server_ = -1; }
#else
        if (server_ >= 0) { ::close(static_cast<int>(server_)); server_ = -1; }
#endif
    }
};

inline tcp_handle tcp_connect(const std::string& host, int port) {
    tcp_init();
#ifdef _WIN32
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) throw std::runtime_error("connect socket failed");
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1)
        throw std::runtime_error("invalid host address");
    addr.sin_port = htons(static_cast<unsigned short>(port));
    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::closesocket(s);
        throw std::runtime_error("connect failed");
    }
    return static_cast<tcp_handle>(s);
#else
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) throw std::runtime_error("connect socket failed");
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1)
        throw std::runtime_error("invalid host address");
    addr.sin_port = htons(static_cast<unsigned short>(port));
    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) { ::close(s); throw std::runtime_error("connect failed"); }
    return static_cast<tcp_handle>(s);
#endif
}

inline void tcp_close(tcp_handle h) {
#ifdef _WIN32
    if (h >= 0) ::closesocket(static_cast<SOCKET>(h));
#else
    if (h >= 0) ::close(static_cast<int>(h));
#endif
}

inline void send_all(tcp_handle h, const std::uint8_t* data, std::size_t len) {
    std::size_t sent = 0;
    while (sent < len) {
#ifdef _WIN32
        int n = ::send(static_cast<SOCKET>(h), reinterpret_cast<const char*>(data + sent),
                       static_cast<int>(len - sent), 0);
#else
        int n = ::send(static_cast<int>(h), data + sent, len - sent, 0);
#endif
        if (n <= 0) throw std::runtime_error("send failed");
        sent += static_cast<std::size_t>(n);
    }
}

inline void recv_exact(tcp_handle h, std::uint8_t* data, std::size_t len) {
    std::size_t got = 0;
    while (got < len) {
#ifdef _WIN32
        int n = ::recv(static_cast<SOCKET>(h), reinterpret_cast<char*>(data + got),
                       static_cast<int>(len - got), 0);
#else
        int n = ::recv(static_cast<int>(h), data + got, len - got, 0);
#endif
        if (n == 0) throw std::runtime_error("connection closed");
        if (n < 0) throw std::runtime_error("recv failed");
        got += static_cast<std::size_t>(n);
    }
}

// Read one frame (header then payload) from a socket using the framed protocol.
inline Frame recv_frame(tcp_handle h) {
    std::vector<std::uint8_t> header(kProtocolHeaderSize);
    recv_exact(h, header.data(), kProtocolHeaderSize);
    ByteReader hr(ByteSpan{header});
    (void)hr.read_u32();  // magic
    (void)hr.read_u16();  // version
    (void)hr.read_u16();  // kind
    const std::uint64_t len = hr.read_u64();
    if (len > kMaxFrameBytes) throw std::runtime_error("frame too large");
    std::vector<std::uint8_t> full(kProtocolHeaderSize + static_cast<std::size_t>(len));
    std::memcpy(full.data(), header.data(), kProtocolHeaderSize);
    if (len > 0) recv_exact(h, full.data() + kProtocolHeaderSize, static_cast<std::size_t>(len));
    auto [frame, err] = decode_frame(ByteSpan(full));
    if (err != FrameError::NONE)
        throw std::runtime_error(std::string("protocol decode error: ") + to_string(err));
    return frame;
}

inline void send_frame(tcp_handle h, MsgKind kind, const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> frame = encode_frame(kind, payload);
    send_all(h, frame.data(), frame.size());
}

}  // namespace stateindex

#endif  // STATEINDEX_TCP_HPP
