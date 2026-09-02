// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include "stateindex/stateindex.hpp"
#include "stateindex/tcp.hpp"
#include "msgs.hpp"

using namespace stateindex;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: state-index-coordinator <port> [persist-file]\n";
        return 2;
    }
    const int port = std::atoi(argv[1]);
    const std::string persist = (argc >= 3) ? argv[2] : "";

    StateIndexEngine engine(CoordinatorEpoch(1));
    if (!persist.empty()) {
        if (std::FILE* f = std::fopen(persist.c_str(), "rb")) {
            std::fclose(f);
            try {
                engine.load(persist);
                std::cout << "coordinator: recovered from " << persist << "\n";
            } catch (const std::exception&) {
                std::cout << "coordinator: no usable persisted file; starting fresh\n";
            }
        }
    }

    try {
        TcpServer server(port);
        std::cout << "coordinator: listening on port " << port << "\n";
        std::cout.flush();
        while (true) {
            tcp_handle conn = server.accept();
            std::cout << "coordinator: accepted connection\n";
            std::cout.flush();
            bool done = false;
            WorkerBootId conn_boot;
            bool has_boot = false;
            while (!done) {
                Frame frame;
                try { frame = recv_frame(conn); }
                catch (const std::exception&) {
                    // A worker process died; its incarnation is no longer live.
                    if (has_boot) engine.unregister_worker(conn_boot);
                    break;
                }
                try {
                    switch (frame.kind) {
                        case MsgKind::HELLO: {
                            auto [w, b] = unpack_hello(frame.payload);
                            engine.register_worker(w, b);
                            conn_boot = b;
                            has_boot = true;
                            send_frame(conn, MsgKind::HELLO_ACK, pack_epoch(engine.epoch()));
                            break;
                        }
                        case MsgKind::REGISTER_STATE: {
                            MutationEnvelope e; StateRecord rec;
                            unpack_register(frame.payload, e, rec);
                            auto v = engine.register_state(rec, e);
                            send_frame(conn, MsgKind::MUTATION_RESULT, pack_mutation_result(v.verdict, v.reason));
                            break;
                        }
                        case MsgKind::ADD_LOCATION: {
                            MutationEnvelope e; StateId s; StateLocation l;
                            unpack_addloc(frame.payload, e, s, l);
                            auto v = engine.add_location(s, l, e);
                            send_frame(conn, MsgKind::MUTATION_RESULT, pack_mutation_result(v.verdict, v.reason));
                            break;
                        }
                        case MsgKind::REMOVE_LOCATION: {
                            MutationEnvelope e; StateId s; PlacementId p;
                            unpack_rmloc(frame.payload, e, s, p);
                            auto v = engine.remove_location(s, p, e);
                            send_frame(conn, MsgKind::MUTATION_RESULT, pack_mutation_result(v.verdict, v.reason));
                            break;
                        }
                        case MsgKind::INVALIDATE: {
                            MutationEnvelope e; InvalidationRecord inv;
                            unpack_invalidate(frame.payload, e, inv);
                            auto v = engine.invalidate(inv, e);
                            send_frame(conn, MsgKind::MUTATION_RESULT, pack_mutation_result(v.verdict, v.reason));
                            break;
                        }
                        case MsgKind::TOMBSTONE: {
                            MutationEnvelope e; TombstoneRecord t;
                            unpack_tombstone(frame.payload, e, t);
                            auto v = engine.tombstone(t, e);
                            send_frame(conn, MsgKind::MUTATION_RESULT, pack_mutation_result(v.verdict, v.reason));
                            break;
                        }
                        case MsgKind::QUERY: {
                            QueryDescriptor q = unpack_query(frame.payload);
                            QueryResult res = engine.query(q);
                            send_frame(conn, MsgKind::QUERY_RESULT, pack_query_result(res));
                            break;
                        }
                        case MsgKind::SAVE: {
                            std::string path = unpack_path(frame.payload);
                            engine.save(path.empty() ? persist : path);
                            send_frame(conn, MsgKind::ACK, {});
                            break;
                        }
                        case MsgKind::LOAD: {
                            std::string path = unpack_path(frame.payload);
                            engine.load(path.empty() ? persist : path);
                            send_frame(conn, MsgKind::ACK, {});
                            break;
                        }
                        case MsgKind::TERMINATE: {
                            if (!persist.empty()) engine.save(persist);
                            send_frame(conn, MsgKind::ACK, {});
                            done = true;
                            std::cout << "coordinator: terminating\n";
                            std::cout.flush();
                            break;
                        }
                        default:
                            send_frame(conn, MsgKind::MUTATION_RESULT,
                                       pack_mutation_result(MutationVerdict::REJECTED_NOT_LIVE, "unsupported message kind"));
                            break;
                    }
                } catch (const std::exception& ex) {
                    send_frame(conn, MsgKind::MUTATION_RESULT,
                               pack_mutation_result(MutationVerdict::REJECTED_NOT_LIVE, std::string("coordinator error: ") + ex.what()));
                }
            }
            if (has_boot) engine.unregister_worker(conn_boot);
            tcp_close(conn);
            if (done) break;
        }
        tcp_shutdown();
    } catch (const std::exception& ex) {
        std::cerr << "coordinator: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
