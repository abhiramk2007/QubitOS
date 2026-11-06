#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include "json.hpp"
#include "dag.hpp"
#include "qasm_parser.hpp"
#include "hardware.hpp"
#include "allocator.hpp"
#include "scheduler.hpp"

using namespace qubitos;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << msg << " at line " << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while(0)

// Unit Test 1: JSON Parser and Serializer
void test_json() {
    std::cout << "[RUNNING] test_json..." << std::endl;
    std::string json_str = R"({
        "name": "test_chip",
        "num_qubits": 5,
        "is_active": true,
        "empty_arr": [],
        "edges": [
            {"a": 0, "b": 1, "error": 0.008},
            {"a": 1, "b": 2, "error": 0.012}
        ]
    })";

    Json j = Json::parse(json_str);
    TEST_ASSERT(j["name"].get<std::string>() == "test_chip", "name mismatch");
    TEST_ASSERT(j["num_qubits"].get<int>() == 5, "num_qubits mismatch");
    TEST_ASSERT(j["is_active"].get<bool>() == true, "bool mismatch");
    TEST_ASSERT(j["edges"].size() == 2, "edges size mismatch");
    TEST_ASSERT(std::abs(j["edges"][0]["error"].get<double>() - 0.008) < 1e-6, "edge error mismatch");

    std::string dumped = j.dump();
    Json j2 = Json::parse(dumped);
    TEST_ASSERT(j2["name"].get<std::string>() == "test_chip", "roundtrip mismatch");
    std::cout << "[PASSED] test_json" << std::endl;
}

// Unit Test 2: QASM Parser with Inline Test Fixtures
void test_qasm_parser() {
    std::cout << "[RUNNING] test_qasm_parser..." << std::endl;

    // Inline test fixture 1: standard circuit with parameters and comments
    std::string qasm_src = 
        "// Unit Test Fixture: bell state with comments and parameters\n"
        "OPENQASM 2.0;\n"
        "include \"qelib1.inc\";\n"
        "qreg q[2];\n"
        "creg c[2];\n"
        "rz(pi/2) q[0];\n"
        "h q[0];\n"
        "cx q[0], q[1];\n"
        "barrier q[0], q[1];\n"
        "measure q[0] -> c[0];\n";

    ParsedCircuit pc = QasmParser::parse_string(qasm_src, "test_fixture_bell");
    TEST_ASSERT(pc.num_qubits == 2, "num_qubits should be 2");
    TEST_ASSERT(pc.dag.gates.size() == 3, "expected 3 gates (rz, h, cx)");
    TEST_ASSERT(pc.dag.two_qubit_pairs.size() == 1, "expected 1 two-qubit pair");
    TEST_ASSERT(pc.dag.two_qubit_pairs[0].q1 == 0 && pc.dag.two_qubit_pairs[0].q2 == 1, "pair should be (0,1)");
    TEST_ASSERT(pc.dag.depth == 3, "depth should be 3 (rz at 1, h at 2, cx at 3)");

    // Inline test fixture 2: multiple quantum registers
    std::string multi_qreg_src = 
        "qreg q[2];\n"
        "qreg r[3];\n"
        "h q[0];\n"
        "cx q[1], r[0];\n"
        "cx r[1], r[2];\n";

    ParsedCircuit pc_multi = QasmParser::parse_string(multi_qreg_src, "test_fixture_multi_qreg");
    TEST_ASSERT(pc_multi.num_qubits == 5, "total qubits across q[2] and r[3] must be 5");
    TEST_ASSERT(pc_multi.dag.two_qubit_pairs.size() == 2, "expected 2 pairs");
    TEST_ASSERT(pc_multi.dag.two_qubit_pairs[0].q1 == 1 && pc_multi.dag.two_qubit_pairs[0].q2 == 2, "cx q[1], r[0] -> (1,2)");
    TEST_ASSERT(pc_multi.dag.two_qubit_pairs[1].q1 == 3 && pc_multi.dag.two_qubit_pairs[1].q2 == 4, "cx r[1], r[2] -> (3,4)");

    std::cout << "[PASSED] test_qasm_parser" << std::endl;
}

// Unit Test 3: CCX 3-qubit Gate Approximation
void test_ccx_approximation() {
    std::cout << "[RUNNING] test_ccx_approximation..." << std::endl;

    std::string toffoli_src = 
        "// Unit Test Fixture: Toffoli ccx decomposition\n"
        "qreg q[3];\n"
        "ccx q[0], q[1], q[2];\n";

    ParsedCircuit pc = QasmParser::parse_string(toffoli_src, "test_fixture_ccx");
    TEST_ASSERT(pc.dag.gates.size() == 1, "1 ccx gate");
    TEST_ASSERT(pc.dag.gates[0].is_approximate == true, "ccx must be flagged approximate");
    TEST_ASSERT(pc.dag.two_qubit_pairs.size() == 2, "ccx must add pairs (q0,q1) and (q1,q2)");
    TEST_ASSERT(pc.dag.two_qubit_pairs[0].q1 == 0 && pc.dag.two_qubit_pairs[0].q2 == 1, "first pair (0,1)");
    TEST_ASSERT(pc.dag.two_qubit_pairs[1].q1 == 1 && pc.dag.two_qubit_pairs[1].q2 == 2, "second pair (1,2)");
    TEST_ASSERT(pc.dag.two_qubit_pairs[0].approximate && pc.dag.two_qubit_pairs[1].approximate, "flagged approximate");

    std::cout << "[PASSED] test_ccx_approximation" << std::endl;
}

// Unit Test 4: DAG Layering and Kahn's Algorithm
void test_dag_and_kahns() {
    std::cout << "[RUNNING] test_dag_and_kahns..." << std::endl;

    CircuitDAG dag;
    dag.num_qubits = 4;
    dag.add_gate("h", {0});       // Layer 1
    dag.add_gate("h", {1});       // Layer 1 (parallel)
    dag.add_gate("cx", {0, 1});   // Layer 2
    dag.add_gate("cx", {2, 3});   // Layer 1 (parallel with h)
    dag.add_gate("cx", {1, 2});   // Layer 3 (depends on cx {0,1} and cx {2,3})

    TEST_ASSERT(dag.depth == 3, "depth must be 3");

    std::vector<std::vector<size_t>> layers = dag.run_kahns_algorithm();
    TEST_ASSERT(layers.size() == 3, "Kahn's algorithm must yield 3 parallel layers");
    // Layer 0 contains {0, 1, 3} (gate IDs for h(0), h(1), cx(2,3))
    TEST_ASSERT(layers[0].size() == 3, "first parallel layer has 3 independent gates");
    // Layer 1 contains {2} (gate ID for cx(0,1))
    TEST_ASSERT(layers[1].size() == 1, "second layer has cx(0,1)");
    // Layer 2 contains {4} (gate ID for cx(1,2))
    TEST_ASSERT(layers[2].size() == 1, "third layer has cx(1,2)");

    std::cout << "[PASSED] test_dag_and_kahns" << std::endl;
}

// Unit Test 5: Hardware Graph BFS Hop Distances and Dijkstra
void test_hardware_algorithms() {
    std::cout << "[RUNNING] test_hardware_algorithms..." << std::endl;

    std::string hw_json = R"({
        "name": "5-qubit Line Chip",
        "num_qubits": 5,
        "edges": [
            {"a": 0, "b": 1, "error": 0.008},
            {"a": 1, "b": 2, "error": 0.012},
            {"a": 2, "b": 3, "error": 0.006},
            {"a": 3, "b": 4, "error": 0.010}
        ]
    })";

    HardwareGraph hw = HardwareGraph::load_from_json(Json::parse(hw_json));

    // Verify BFS hop distances
    TEST_ASSERT(hw.get_hop_distance(0, 0) == 0, "hop(0,0)=0");
    TEST_ASSERT(hw.get_hop_distance(0, 1) == 1, "hop(0,1)=1");
    TEST_ASSERT(hw.get_hop_distance(0, 4) == 4, "hop(0,4)=4");
    TEST_ASSERT(hw.get_hop_distance(1, 3) == 2, "hop(1,3)=2");

    // Verify Dijkstra minimum error costs
    double path_0_2 = hw.min_error_dist[0][2];
    TEST_ASSERT(std::abs(path_0_2 - (0.008 + 0.012)) < 1e-9, "Dijkstra 0->2 error should be 0.020");
    double path_0_4 = hw.min_error_dist[0][4];
    TEST_ASSERT(std::abs(path_0_4 - (0.008 + 0.012 + 0.006 + 0.010)) < 1e-9, "Dijkstra 0->4 error should be 0.036");

    // Verify SWAP estimation under identity mapping
    std::vector<GatePair> pairs = {{0, 1, false}, {0, 2, false}, {0, 3, false}};
    std::vector<size_t> id_map = {0, 1, 2, 3};
    size_t swaps = hw.compute_swaps(pairs, id_map);
    // (0,1)->0, (0,2)->1, (0,3)->2 => total swaps = 3
    TEST_ASSERT(swaps == 3, "swaps for star-4 should be 3");

    std::cout << "[PASSED] test_hardware_algorithms" << std::endl;
}

// Unit Test 6: Allocator Subgraph BFS and Tie-Breaking
void test_allocator() {
    std::cout << "[RUNNING] test_allocator..." << std::endl;

    std::string hw_json = R"({
        "name": "5-qubit Line Chip",
        "num_qubits": 5,
        "edges": [
            {"a": 0, "b": 1, "error": 0.008},
            {"a": 1, "b": 2, "error": 0.012},
            {"a": 2, "b": 3, "error": 0.006},
            {"a": 3, "b": 4, "error": 0.010}
        ]
    })";
    HardwareGraph hw = HardwareGraph::load_from_json(Json::parse(hw_json));

    // Case A: All qubits free, allocate 2 qubits for pair (0,1)
    std::vector<bool> is_free(5, true);
    std::vector<GatePair> pairs_2 = {{0, 1, false}};
    AllocationResult res_a = Allocator::allocate(hw, is_free, 2, pairs_2);
    TEST_ASSERT(res_a.success, "allocation must succeed");
    TEST_ASSERT(res_a.block.size() == 2, "block size 2");
    // s=0 gives {0,1} swaps 0; s=1 gives {1,2} swaps 0. Tie-break chooses lowest s=0 -> {0,1}
    TEST_ASSERT(res_a.block[0] == 0 && res_a.block[1] == 1, "tie-breaking chooses block {0,1}");
    TEST_ASSERT(res_a.swap_cost == 0, "swap cost 0");

    // Case B: Qubits 0 and 1 occupied, free={2,3,4}, allocate 3 qubits for star-3
    is_free[0] = false;
    is_free[1] = false;
    std::vector<GatePair> pairs_star3 = {{0, 1, false}, {0, 2, false}};
    AllocationResult res_b = Allocator::allocate(hw, is_free, 3, pairs_star3);
    TEST_ASSERT(res_b.success, "allocation must succeed on {2,3,4}");
    TEST_ASSERT(res_b.block == (std::vector<size_t>{2, 3, 4}), "block should be {2,3,4}");
    TEST_ASSERT(res_b.swap_cost == 1, "swap cost on {2,3,4} is 1");

    // Case C: Fragmented free qubits {0, 2, 4} (not connected)
    std::vector<bool> is_free_frag = {true, false, true, false, true};
    AllocationResult res_c = Allocator::allocate(hw, is_free_frag, 2, pairs_2);
    TEST_ASSERT(!res_c.success, "cannot allocate 2 connected qubits when free qubits are disjoint");

    std::cout << "[PASSED] test_allocator" << std::endl;
}

// Unit Test 7: Full Reference Simulation via Inline Test Fixtures
void test_reference_simulation() {
    std::cout << "[RUNNING] test_reference_simulation..." << std::endl;

    std::string hw_json = R"raw({
        "name": "5-qubit Line Chip (illustrative values)",
        "num_qubits": 5,
        "edges": [
            {"a": 0, "b": 1, "error": 0.008},
            {"a": 1, "b": 2, "error": 0.012},
            {"a": 2, "b": 3, "error": 0.006},
            {"a": 3, "b": 4, "error": 0.010}
        ]
    })raw";
    HardwareGraph hw = HardwareGraph::load_from_json(Json::parse(hw_json));

    // Define the 5 reference circuits as test fixtures (in memory only, never saved to jobs/)
    struct FixtureDef {
        std::string file;
        size_t arrival;
        size_t priority;
        size_t deadline;
        std::string qasm;
        size_t exp_qubits;
        size_t exp_depth;
        size_t exp_swaps;
        size_t exp_est_runtime;
    };

    std::vector<FixtureDef> fixtures = {
        {
            "ghz4_star.qasm", 0, 2, 20,
            "qreg q[4];\nh q[0];\ncx q[0], q[1];\ncx q[0], q[2];\ncx q[0], q[3];\n",
            4, 4, 3, 11
        },
        {
            "bell.qasm", 1, 1, 12,
            "qreg q[2];\nh q[0];\ncx q[0], q[1];\n",
            2, 2, 0, 4
        },
        {
            "ghz3_star.qasm", 2, 3, 14,
            "qreg q[3];\nh q[0];\ncx q[0], q[1];\ncx q[0], q[2];\n",
            3, 3, 1, 7
        },
        {
            "ghz4_chain.qasm", 3, 2, 25,
            "qreg q[4];\nh q[0];\ncx q[0], q[1];\ncx q[1], q[2];\ncx q[2], q[3];\n",
            4, 4, 0, 8
        },
        {
            "ends5.qasm", 4, 3, 40,
            "qreg q[5];\nh q[0];\ncx q[0], q[4];\ncx q[1], q[3];\ncx q[2], q[4];\n",
            5, 3, 5, 11
        }
    };

    std::vector<Job> jobs;
    for (size_t i = 0; i < fixtures.size(); ++i) {
        const auto& fix = fixtures[i];
        ParsedCircuit pc = QasmParser::parse_string(fix.qasm, fix.file);

        Job j;
        j.id = i;
        j.manifest_index = i;
        j.file = fix.file;
        j.num_qubits = pc.num_qubits;
        j.depth = pc.dag.depth;
        j.two_qubit_pairs = pc.dag.two_qubit_pairs;
        j.arrival = fix.arrival;
        j.priority = fix.priority;
        j.deadline = fix.deadline;

        std::vector<size_t> id_map(j.num_qubits);
        for (size_t q = 0; q < j.num_qubits; ++q) id_map[q] = q;
        j.est_swaps = hw.compute_swaps(j.two_qubit_pairs, id_map);
        j.est_runtime = j.depth * 2 + j.est_swaps * 1;
        j.noise_cost = hw.compute_noise_cost(j.two_qubit_pairs, id_map);

        TEST_ASSERT(j.num_qubits == fix.exp_qubits, "qubits mismatch for " + fix.file);
        TEST_ASSERT(j.depth == fix.exp_depth, "depth mismatch for " + fix.file);
        TEST_ASSERT(j.est_swaps == fix.exp_swaps, "estSwaps mismatch for " + fix.file);
        TEST_ASSERT(j.est_runtime == fix.exp_est_runtime, "estRuntime mismatch for " + fix.file);

        jobs.push_back(std::move(j));
    }

    SimulationEngine engine(hw, 2, 1);
    std::vector<ModeRunResult> runs = engine.run_all_modes(jobs);
    TEST_ASSERT(runs.size() == 5, "must produce 5 simulation runs");

    // Mode 1: FCFS, one job at a time -> 13.6, 21.8, 41, 4, 9
    TEST_ASSERT(std::abs(runs[0].stats.avgWT - 13.6) < 1e-4, "Mode 1 avgWT must be 13.6");
    TEST_ASSERT(std::abs(runs[0].stats.avgTAT - 21.8) < 1e-4, "Mode 1 avgTAT must be 21.8");
    TEST_ASSERT(runs[0].stats.makespan == 41, "Mode 1 makespan must be 41");
    TEST_ASSERT(runs[0].stats.misses == 4, "Mode 1 deadline misses must be 4");
    TEST_ASSERT(runs[0].stats.swaps == 9, "Mode 1 total swaps must be 9");

    // Mode 2: FCFS + packing -> 11.2, 19.4, 37, 3, 9
    TEST_ASSERT(std::abs(runs[1].stats.avgWT - 11.2) < 1e-4, "Mode 2 avgWT must be 11.2");
    TEST_ASSERT(std::abs(runs[1].stats.avgTAT - 19.4) < 1e-4, "Mode 2 avgTAT must be 19.4");
    TEST_ASSERT(runs[1].stats.makespan == 37, "Mode 2 makespan must be 37");
    TEST_ASSERT(runs[1].stats.misses == 3, "Mode 2 deadline misses must be 3");
    TEST_ASSERT(runs[1].stats.swaps == 9, "Mode 2 total swaps must be 9");

    // Mode 3: Priority + packing -> 11.8, 20.0, 37, 3, 9
    TEST_ASSERT(std::abs(runs[2].stats.avgWT - 11.8) < 1e-4, "Mode 3 avgWT must be 11.8");
    TEST_ASSERT(std::abs(runs[2].stats.avgTAT - 20.0) < 1e-4, "Mode 3 avgTAT must be 20.0");
    TEST_ASSERT(runs[2].stats.makespan == 37, "Mode 3 makespan must be 37");
    TEST_ASSERT(runs[2].stats.misses == 3, "Mode 3 deadline misses must be 3");
    TEST_ASSERT(runs[2].stats.swaps == 9, "Mode 3 total swaps must be 9");

    // Mode 4: SJF + packing -> 11.2, 19.4, 37, 3, 9
    TEST_ASSERT(std::abs(runs[3].stats.avgWT - 11.2) < 1e-4, "Mode 4 avgWT must be 11.2");
    TEST_ASSERT(std::abs(runs[3].stats.avgTAT - 19.4) < 1e-4, "Mode 4 avgTAT must be 19.4");
    TEST_ASSERT(runs[3].stats.makespan == 37, "Mode 4 makespan must be 37");
    TEST_ASSERT(runs[3].stats.misses == 3, "Mode 4 deadline misses must be 3");
    TEST_ASSERT(runs[3].stats.swaps == 9, "Mode 4 total swaps must be 9");

    // Mode 5: Custom + packing -> 11.2, 19.4, 37, 3, 9
    TEST_ASSERT(std::abs(runs[4].stats.avgWT - 11.2) < 1e-4, "Mode 5 avgWT must be 11.2");
    TEST_ASSERT(std::abs(runs[4].stats.avgTAT - 19.4) < 1e-4, "Mode 5 avgTAT must be 19.4");
    TEST_ASSERT(runs[4].stats.makespan == 37, "Mode 5 makespan must be 37");
    TEST_ASSERT(runs[4].stats.misses == 3, "Mode 5 deadline misses must be 3");
    TEST_ASSERT(runs[4].stats.swaps == 9, "Mode 5 total swaps must be 9");

    std::cout << "[PASSED] test_reference_simulation (All 5 modes matched reference metrics exactly!)" << std::endl;
}

int main() {
    std::cout << "========================================\n";
    std::cout << "       Running QubitOS Unit Tests       \n";
    std::cout << "========================================\n";

    test_json();
    test_qasm_parser();
    test_ccx_approximation();
    test_dag_and_kahns();
    test_hardware_algorithms();
    test_allocator();
    test_reference_simulation();

    std::cout << "========================================\n";
    std::cout << "    ALL UNIT TESTS PASSED (7 / 7)       \n";
    std::cout << "========================================\n";
    return 0;
}
