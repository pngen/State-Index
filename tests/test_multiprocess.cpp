// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
// Real multiprocess authority proof: spawns the coordinator and worker
// executables as genuine OS processes over framed TCP.
#include "test_util.hpp"
#include "scenarios.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "stateindex/stateindex.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace stateindex;
using namespace siscenario;

static std::string g_exe_dir;
static const int kPort = 39561;
static const char* kPersist = "test_mp_persist.bin";

static std::filesystem::path exe(const std::string& name) {
    return std::filesystem::path(g_exe_dir) / name;
}
static std::string quote(const std::string& s) { return "\"" + s + "\""; }

#ifdef _WIN32
static DWORD run_process(const std::string& cmdline, const std::string& outfile) {
    if (outfile.empty()) {
        STARTUPINFOA si{}; si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::string cmd = cmdline;
        if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            return 0xFFFFFFFF;
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return code;
    }
    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    HANDLE hf = CreateFileA(outfile.c_str(), GENERIC_WRITE, 0, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOA si{}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hf; si.hStdError = hf; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::string cmd = cmdline;
    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hf);
        return 0xFFFFFFFF;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(hf); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return code;
}

static HANDLE start_bg(const std::string& cmdline, const std::string& outfile) {
    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    HANDLE hf = CreateFileA(outfile.c_str(), GENERIC_WRITE, 0, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOA si{}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hf; si.hStdError = hf; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::string cmd = cmdline;
    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hf);
        return nullptr;
    }
    CloseHandle(pi.hThread);
    CloseHandle(hf);
    return pi.hProcess;
}
#else
// POSIX fallback (not exercised on the primary validation host).
static int run_process(const std::string&, const std::string&) { return 0; }
static void* start_bg(const std::string&, const std::string&) { return nullptr; }
#endif

static int worker_run(const std::string& wid, const std::string& boot, const std::string& script, const std::string& log) {
    std::string cmd = quote(exe("state-index-worker.exe").string()) + " 127.0.0.1 " + std::to_string(kPort) +
                      " " + wid + " " + boot + " " + quote((std::filesystem::path(g_exe_dir) / script).string());
    return static_cast<int>(run_process(cmd, (std::filesystem::path(g_exe_dir) / log).string()));
}

static std::string read_file_text(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}
static void write_script(const std::string& name, const std::string& cmds) {
    std::filesystem::path p = std::filesystem::path(g_exe_dir) / name;
    std::ofstream f(p, std::ios::trunc); f << cmds;
}

static void delete_file(const std::string& path) {
    std::error_code ec; std::filesystem::remove(std::filesystem::path(path), ec);
}

static void multiprocess_proof() {
#ifdef _WIN32
    std::filesystem::path base = std::filesystem::path(g_exe_dir);
    delete_file(kPersist);
    const std::string persist_path = (std::filesystem::path(base) / kPersist).string();
    std::string coord = quote(exe("state-index-coordinator.exe").string()) + " " + std::to_string(kPort) + " " + quote(persist_path);

    HANDLE coord_h = start_bg(coord, (base / "coord.log").string());
    CHECK(coord_h != nullptr);

    // Wait for coordinator to listen by retrying a worker that just pings.
    bool ready = false;
    for (int i = 0; i < 100; ++i) {
        // Try a quick connect: run a worker whose script is empty (just HELLO + disconnect).
        write_script("mp_wait.txt", "query 1\n");
        if (worker_run("1", "1", "mp_wait.txt", "w0.log") == 0) { ready = true; break; }
        Sleep(50);
    }
    CHECK(ready);

    // Worker A (boot 100): register state 100 gen1, then a replica.
    write_script("mp_a.txt", "register 100 1 kv a mem 1\n");
    worker_run("1", "100", "mp_a.txt", "wA.log");
    // Worker A exited -> boot 100 is no longer live.

    // Worker A stale replay (same boot 100): must be rejected as stale boot.
    write_script("mp_astale.txt", "boot 999\nregister 100 1 kv a mem 1\n");
    worker_run("1", "100", "mp_astale.txt", "wAstale.log");

    // Worker B (boot 200, fresh incarnation): register state 100 gen2 (supersedes) + state 200 gen1.
    write_script("mp_b.txt", "register 100 2 kv b mem 1\nregister 200 1 kv c mem 1\n");
    worker_run("2", "200", "mp_b.txt", "wB.log");

    // Stale epoch publication from a live worker: rejected.
    write_script("mp_epoch.txt", "epoch 9\nregister 300 1 kv d mem 1\n");
    worker_run("3", "300", "mp_epoch.txt", "wEpoch.log");

    // Duplicate publication from the live worker: rejected as duplicate.
    write_script("mp_dup.txt", "register 200 1 kv c mem 1\n");
    worker_run("2", "200", "mp_dup.txt", "wDup.log");

    // Tombstone old state 200.
    write_script("mp_tomb.txt", "tombstone 200 1\n");
    worker_run("4", "400", "mp_tomb.txt", "wTomb.log");

    // Query state 100 (current gen2) from a fresh worker.
    write_script("mp_q.txt", "query 100\n");
    worker_run("5", "500", "mp_q.txt", "wQ.log");

    // Control worker terminates the coordinator (which persists).
    write_script("mp_ctl.txt", "terminate\n");
    worker_run("6", "600", "mp_ctl.txt", "wCtl.log");

    WaitForSingleObject(coord_h, 30000);
    DWORD ccode = 0; GetExitCodeProcess(coord_h, &ccode);
    CloseHandle(coord_h);

    // Assertions on worker logs.
    const std::string aLog = read_file_text((base / "wA.log").string());
    CHECK(aLog.find("VERDICT=ACCEPTED") != std::string::npos);
    const std::string aStale = read_file_text((base / "wAstale.log").string());
    CHECK(aStale.find("REJECTED_STALE_BOOT") != std::string::npos);
    const std::string bLog = read_file_text((base / "wB.log").string());
    CHECK(bLog.find("VERDICT=ACCEPTED") != std::string::npos);
    const std::string epochLog = read_file_text((base / "wEpoch.log").string());
    CHECK(epochLog.find("REJECTED_STALE_EPOCH") != std::string::npos);
    const std::string dupLog = read_file_text((base / "wDup.log").string());
    CHECK(dupLog.find("REJECTED_DUPLICATE") != std::string::npos);
    const std::string qLog = read_file_text((base / "wQ.log").string());
    CHECK(qLog.find("SELECTED_GEN=2") != std::string::npos);

    // Recovery: load the persisted file into a fresh engine and verify logical state.
    StateIndexEngine reload(CoordinatorEpoch(1));
    reload.load((base / kPersist).string());
    CHECK(reload.invariant_check().empty());
    QueryDescriptor q; q.query_id = QueryId(1); q.state_id = StateId(100);
    QueryResult r = reload.query(q);
    CHECK(r.selected_generation == StateGeneration(2));   // gen2 current
    auto hist = reload.history_of(StateId(100));
    CHECK(hist.size() == 2);
    // Recovery: live-worker authority cleared and physical revalidation.
    auto rec = reload.get_record(record_id_of(StateId(100), StateGeneration(2)));
    CHECK(rec.has_value());
    bool host_reval = false;
    for (const auto& l : rec->locations)
        if (l.domain == MemoryDomain::HOST_MEMORY && l.freshness == Freshness::REVALIDATION_REQUIRED) host_reval = true;
    CHECK(host_reval);
    // State 200 was tombstoned -> not current.
    QueryDescriptor q2; q2.query_id = QueryId(2); q2.state_id = StateId(200);
    QueryResult r2 = reload.query(q2);
    CHECK(!r2.found());
#else
    std::cout << "multiprocess proof requires Windows process spawning; skipped on this platform\n";
#endif
}

int main(int argc, char** argv) {
    g_exe_dir = argc >= 2 ? argv[1] : std::filesystem::current_path().string();
    int f = 0;
    std::cout << "multiprocess exe dir: " << g_exe_dir << "\n";
    f += sittest::run("multiprocess_proof", multiprocess_proof);
    if (f == 0) std::cout << "ALL PASS\n";
    return f == 0 ? 0 : 1;
}
