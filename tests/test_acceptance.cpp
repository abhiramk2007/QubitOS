#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include "json.hpp"
#include "dag.hpp"
#include "qasm_parser.hpp"
#include "hardware.hpp"
#include "allocator.hpp"
#include "scheduler.hpp"

namespace fs = std::filesystem;
using namespace qubitos;

#define ACCEPTANCE_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[ACCEPTANCE TEST FAILED] " << msg << " at line " << __LINE__ << std::endl; \
            return 1; \
        } \
    } while(0)

int main(int argc, char* argv[]) {
    std::string jobs_dir = "jobs/";
    std::string hw_path = "hardware.json";

    if (argc > 1) jobs_dir = argv[1];
    if (argc > 2) hw_path = argv[2];

    std::cout << "========================================\n";
    std::cout << "    QubitOS Acceptance Test Suite       \n";
    std::cout << "========================================\n";

    // Expected 5 reference files
    const std::vector<std::string> expected_files = {
        "ghz4_star.qasm",
        "bell.qasm",
        "ghz3_star.qasm",
        "ghz4_chain.qasm",
        "ends5.qasm"
    };

    // Check if the expected files exist in jobs/
    bool all_exist = true;
    for (const auto& file : expected_files) {
        fs::path p = fs::path(jobs_dir) / file;
        if (!fs::exists(p)) {
            all_exist = false;
            break;
        }
    }

    if (!all_exist) {
        std::cout << "[SKIPPED] waiting for user's .qasm files" << std::endl;
        std::cout << "Acceptance tests will automatically run once user drops Composer files into " << jobs_dir << std::endl;
        return 0; // Return success/skip
    }

    std::cout << "[INFO] Found all 5 expected .qasm files in " << jobs_dir << ". Executing full acceptance validation...\n";

    // 1. Load hardware
    HardwareGraph hw;
    try {
        hw = HardwareGraph::load_from_json_file(hw_path);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load hardware from " << hw_path << ": " << e.what() << std::endl;
        return 1;
    }

    // 2. Read manifest
    std::string manifest_path = (fs::path(jobs_dir) / "manifest.csv").string();
    std::unordered_map<std::string, std::tuple<size_t, size_t, size_t>> manifest_data;
    if (fs::exists(manifest_path)) {
        std::ifstream mfile(manifest_path);
        std::string line;
        bool first = true;
        while (std::getline(mfile, line)) {
            if (line.empty()) continue;
            if (first && line.find("file") != std::string::npos) { first = false; continue; }
            std::stringstream ss(line);
            std::string fn, arr, prio, ddl;
            if (std::getline(ss, fn, ',') && std::getline(ss, arr, ',') &&
                std::getline(ss, prio, ',') && std::getline(ss, ddl, ',')) {
                fn.erase(0, fn.find_first_not_of(" \r\n\t"));
                fn.erase(fn.find_last_not_of(" \r\n\t") + 1);
                manifest_data[fn] = {std::stoul(arr), std::stoul(prio), std::stoul(ddl)};
            }
        }
    }

    // Expected per-circuit metrics: (qubits, depth, estSwaps, estRuntime)
    struct CircuitExpected {
        std::string name;
        size_t qubits;
        size_t depth;
        size_t est_swaps;
        size_t est_runtime;
        std::vector<std::pair<size_t, size_t>> pairs;
    };

    std::unordered_map<std::string, CircuitExpected> expected_circuits = {
        {"bell.qasm", {"bell", 2, 2, 0, 4, {{0, 1}}}},
        {"ghz3_star.qasm", {"ghz3_star", 3, 3, 1, 7, {{0, 1}, {0, 2}}}},
        {"ghz4_chain.qasm", {"ghz4_chain", 4, 4, 0, 8, {{0, 1}, {1, 2}, {2, 3}}}},
        {"ghz4_star.qasm", {"ghz4_star", 4, 4, 3, 11, {{0, 1}, {0, 2}, {0, 3}}}},
        {"ends5.qasm", {"ends5", 5, 3, 5, 11, {{0, 4}, {1, 3}, {2, 4}}}}
    };

    std::vector<Job> jobs;
    for (size_t i = 0; i < expected_files.size(); ++i) {
        const std::string& fname = expected_files[i];
        std::string full_path = (fs::path(jobs_dir) / fname).string();
        ParsedCircuit pc = QasmParser::parse_file(full_path);

        const auto& exp = expected_circuits[fname];
        ACCEPTANCE_ASSERT(pc.num_qubits == exp.qubits, "qubits mismatch for " + fname);
        ACCEPTANCE_ASSERT(pc.dag.depth == exp.depth, "depth mismatch for " + fname);

        std::vector<size_t> id_map(pc.num_qubits);
        for (size_t q = 0; q < pc.num_qubits; ++q) id_map[q] = q;
        size_t est_swaps = hw.compute_swaps(pc.dag.two_qubit_pairs, id_map);
        size_t est_runtime = pc.dag.depth * 2 + est_swaps * 1;

        ACCEPTANCE_ASSERT(est_swaps == exp.est_swaps, "estSwaps mismatch for " + fname);
        ACCEPTANCE_ASSERT(est_runtime == exp.est_runtime, "estRuntime mismatch for " + fname);

        Job j;
        j.id = i;
        j.manifest_index = i;
        j.file = fname;
        j.num_qubits = pc.num_qubits;
        j.depth = pc.dag.depth;
        j.two_qubit_pairs = pc.dag.two_qubit_pairs;
        j.est_swaps = est_swaps;
        j.est_runtime = est_runtime;
        j.noise_cost = hw.compute_noise_cost(pc.dag.two_qubit_pairs, id_map);

        if (manifest_data.find(fname) != manifest_data.end()) {
            auto [arr, prio, ddl] = manifest_data[fname];
            j.arrival = arr;
            j.priority = prio;
            j.deadline = ddl;
        } else {
            j.arrival = i;
            j.priority = 1;
            j.deadline = j.arrival + 30;
        }

        jobs.push_back(std::move(j));
    }

    std::cout << "[PASSED] All 5 individual circuit metrics validated against reference specifications.\n";

    // 3. Run all 5 scheduling modes
    SimulationEngine engine(hw, 2, 1);
    std::vector<ModeRunResult> runs = engine.run_all_modes(jobs);

    ACCEPTANCE_ASSERT(runs.size() == 5, "Must produce 5 runs");

    // Mode 1: FCFS, one job at a time -> 13.6, 21.8, 41, 4, 9
    ACCEPTANCE_ASSERT(std::abs(runs[0].stats.avgWT - 13.6) < 1e-4, "Mode 1 avgWT mismatch");
    ACCEPTANCE_ASSERT(std::abs(runs[0].stats.avgTAT - 21.8) < 1e-4, "Mode 1 avgTAT mismatch");
    ACCEPTANCE_ASSERT(runs[0].stats.makespan == 41, "Mode 1 makespan mismatch");
    ACCEPTANCE_ASSERT(runs[0].stats.misses == 4, "Mode 1 misses mismatch");
    ACCEPTANCE_ASSERT(runs[0].stats.swaps == 9, "Mode 1 swaps mismatch");
    std::cout << "[PASSED] Mode 1: FCFS one at a time (13.6, 21.8, 41, 4, 9)\n";

    // Mode 2: FCFS + packing -> 11.2, 19.4, 37, 3, 9
    ACCEPTANCE_ASSERT(std::abs(runs[1].stats.avgWT - 11.2) < 1e-4, "Mode 2 avgWT mismatch");
    ACCEPTANCE_ASSERT(std::abs(runs[1].stats.avgTAT - 19.4) < 1e-4, "Mode 2 avgTAT mismatch");
    ACCEPTANCE_ASSERT(runs[1].stats.makespan == 37, "Mode 2 makespan mismatch");
    ACCEPTANCE_ASSERT(runs[1].stats.misses == 3, "Mode 2 misses mismatch");
    ACCEPTANCE_ASSERT(runs[1].stats.swaps == 9, "Mode 2 swaps mismatch");
    std::cout << "[PASSED] Mode 2: FCFS + packing (11.2, 19.4, 37, 3, 9)\n";

    // Mode 3: Priority + packing -> 11.8, 20.0, 37, 3, 9
    ACCEPTANCE_ASSERT(std::abs(runs[2].stats.avgWT - 11.8) < 1e-4, "Mode 3 avgWT mismatch");
    ACCEPTANCE_ASSERT(std::abs(runs[2].stats.avgTAT - 20.0) < 1e-4, "Mode 3 avgTAT mismatch");
    ACCEPTANCE_ASSERT(runs[2].stats.makespan == 37, "Mode 3 makespan mismatch");
    ACCEPTANCE_ASSERT(runs[2].stats.misses == 3, "Mode 3 misses mismatch");
    ACCEPTANCE_ASSERT(runs[2].stats.swaps == 9, "Mode 3 swaps mismatch");
    std::cout << "[PASSED] Mode 3: Priority + packing (11.8, 20.0, 37, 3, 9)\n";

    // Mode 4: SJF + packing -> 11.2, 19.4, 37, 3, 9
    ACCEPTANCE_ASSERT(std::abs(runs[3].stats.avgWT - 11.2) < 1e-4, "Mode 4 avgWT mismatch");
    ACCEPTANCE_ASSERT(std::abs(runs[3].stats.avgTAT - 19.4) < 1e-4, "Mode 4 avgTAT mismatch");
    ACCEPTANCE_ASSERT(runs[3].stats.makespan == 37, "Mode 4 makespan mismatch");
    ACCEPTANCE_ASSERT(runs[3].stats.misses == 3, "Mode 4 misses mismatch");
    ACCEPTANCE_ASSERT(runs[3].stats.swaps == 9, "Mode 4 swaps mismatch");
    std::cout << "[PASSED] Mode 4: SJF + packing (11.2, 19.4, 37, 3, 9)\n";

    // Mode 5: Custom + packing -> 11.2, 19.4, 37, 3, 9
    ACCEPTANCE_ASSERT(std::abs(runs[4].stats.avgWT - 11.2) < 1e-4, "Mode 5 avgWT mismatch");
    ACCEPTANCE_ASSERT(std::abs(runs[4].stats.avgTAT - 19.4) < 1e-4, "Mode 5 avgTAT mismatch");
    ACCEPTANCE_ASSERT(runs[4].stats.makespan == 37, "Mode 5 makespan mismatch");
    ACCEPTANCE_ASSERT(runs[4].stats.misses == 3, "Mode 5 misses mismatch");
    ACCEPTANCE_ASSERT(runs[4].stats.swaps == 9, "Mode 5 swaps mismatch");
    std::cout << "[PASSED] Mode 5: Custom + packing (11.2, 19.4, 37, 3, 9)\n";

    std::cout << "========================================\n";
    std::cout << "    ALL ACCEPTANCE TESTS PASSED!        \n";
    std::cout << "========================================\n";
    return 0;
}

