#ifndef QUBITOS_HARDWARE_HPP
#define QUBITOS_HARDWARE_HPP

#include <string>
#include <vector>
#include <utility>
#include <cstddef>
#include "json.hpp"
#include "dag.hpp"

namespace qubitos {

struct HardwareEdge {
    size_t a = 0;
    size_t b = 0;
    double error = 0.0;
};

// Represents the quantum processing unit (QPU) physical topology.
// Implements:
// 1. Graph with adjacency list for physical qubit connectivity
// 2. BFS from every qubit for all-pairs shortest hop distance
// 3. Dijkstra's algorithm (edge weight = error) for noiseCost metric
class HardwareGraph {
public:
    std::string name;
    size_t num_qubits = 0;
    std::vector<HardwareEdge> edge_list;

    // Adjacency list: adj[u] contains pairs (neighbor_v, error_weight)
    std::vector<std::vector<std::pair<size_t, double>>> adj;

    // Precomputed all-pairs hop distance matrix via BFS
    std::vector<std::vector<size_t>> hop_dist;

    // Precomputed all-pairs minimum error path cost matrix via Dijkstra
    std::vector<std::vector<double>> min_error_dist;

    HardwareGraph() = default;

    // Loads hardware definition from hardware.json
    static HardwareGraph load_from_json_file(const std::string& filepath);
    static HardwareGraph load_from_json(const Json& j);

    // Computes all-pairs hop distances using Breadth-First Search (BFS)
    void compute_all_pairs_bfs();

    // Computes all-pairs minimum noise paths using Dijkstra's algorithm
    void compute_all_pairs_dijkstra();

    // Returns hop distance between two physical qubits
    size_t get_hop_distance(size_t p1, size_t p2) const;

    // Calculates estimated SWAPs for a given circuit gate-pair list and placement mapping
    // mapping[logical_qubit] = physical_qubit
    size_t compute_swaps(const std::vector<GatePair>& pairs, const std::vector<size_t>& mapping) const;

    // Computes noiseCost (sum of cheapest weighted Dijkstra paths over all 2-qubit gates)
    double compute_noise_cost(const std::vector<GatePair>& pairs, const std::vector<size_t>& mapping) const;
};

} // namespace qubitos

#endif // QUBITOS_HARDWARE_HPP

