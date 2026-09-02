// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef STATEINDEX_TEST_UTIL_HPP
#define STATEINDEX_TEST_UTIL_HPP

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace sittest {

inline int g_failures = 0;
inline int g_checks = 0;

inline void report(bool ok, const char* expr, const char* file, int line, const std::string& msg) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cout << "FAIL [" << file << ":" << line << "] " << expr;
        if (!msg.empty()) std::cout << " :: " << msg;
        std::cout << "\n";
    }
}

// Run a single test clump; returns number of failures.
inline int run(const char* name, void (*fn)()) {
    g_failures = g_checks = 0;
    std::cout << "---- " << name << " ----\n";
    try {
        fn();
    } catch (const std::exception& e) {
        ++g_failures;
        std::cout << "FAIL uncaught exception: " << e.what() << "\n";
    } catch (...) {
        ++g_failures;
        std::cout << "FAIL uncaught exception\n";
    }
    if (g_failures == 0) std::cout << "[PASS] " << name << " (" << g_checks << " checks)\n";
    else std::cout << "[FAIL] " << name << " (" << g_failures << " failures, " << g_checks << " checks)\n";
    return g_failures;
}

}  // namespace sittest

#define CHECK(cond) sittest::report(static_cast<bool>(cond), #cond, __FILE__, __LINE__, "")
#define CHECK_MSG(cond, msg) sittest::report(static_cast<bool>(cond), #cond, __FILE__, __LINE__, (msg))
#define CHECK_EQ(a, b) sittest::report(static_cast<bool>((a) == (b)), #a " == " #b, __FILE__, __LINE__, "")
#define CHECK_NE(a, b) sittest::report(static_cast<bool>((a) != (b)), #a " != " #b, __FILE__, __LINE__, "")

#endif  // STATEINDEX_TEST_UTIL_HPP
