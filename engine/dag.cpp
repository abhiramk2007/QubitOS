#include "dag.hpp"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <queue>

namespace qubitos {

void CircuitDAG::add_gate(const std::string& name, const std::vector<size_t>& qubits) {
    if (qubits.empty()) return;

    // Ensure tracking vector is sized for all qubits encountered
    for (size_t q : qubits) {
        if (q >= num_qubits) {
            num_qubits = q + 1;
        }
    }
    if (last_gate_on_qubit.size() < num_qubits) {
        last_gate_on_qubit.resize(num_qubits, -1);
    }

    Gate g;
    g.id = gates.size();
    g.name = name;
    g.qubits = qubits;

    size_t prev_max_layer = 0;
    std::unordered_set<size_t> predecessor_ids;

    // Dependency analysis: each qubit's prior gate must precede this gate
    for (size_t q : qubits) {
        int prev_id = last_gate_on_qubit[q];
        if (prev_id >= 0 && static_cast<size_t>(prev_id) < gates.size()) {
            predecessor_ids.insert(static_cast<size_t>(prev_id));
            if (gates[prev_id].layer > prev_max_layer) {
                prev_max_layer = gates[prev_id].layer;
            }
        }
    }

    // Layer assignment: layer(gate) = 1 + max(layer of previous gate on each of its qubits)
    g.layer = prev_max_layer + 1;
    if (g.layer > depth) {
        depth = g.layer;
    }

    // Update adjacency lists
    adj.resize(g.id + 1);
    in_edges.resize(g.id + 1);
    for (size_t pred : predecessor_ids) {
        adj[pred].push_back(g.id);
        in_edges[g.id].push_back(pred);
    }

    // Update last gate on these qubits
    for (size_t q : qubits) {
        last_gate_on_qubit[q] = static_cast<int>(g.id);
    }

    // Handle 2-qubit interactions and 3-qubit approximations
    if (qubits.size() == 2) {
        two_qubit_pairs.push_back({qubits[0], qubits[1], false});
    } else if (qubits.size() == 3) {
        // ccx / Toffoli approximation: decomposed into interaction pairs (q0, q1) and (q1, q2)
        g.is_approximate = true;
        two_qubit_pairs.push_back({qubits[0], qubits[1], true});
        two_qubit_pairs.push_back({qubits[1], qubits[2], true});
    }

    gates.push_back(std::move(g));
}

std::vector<std::vector<size_t>> CircuitDAG::run_kahns_algorithm() {
    size_t n = gates.size();
    if (n == 0) return {};

    std::vector<size_t> in_deg(n, 0);
    for (size_t u = 0; u < n; ++u) {
        in_deg[u] = in_edges[u].size();
    }

    std::vector<std::vector<size_t>> parallel_layers;
    std::vector<size_t> current_level;

    for (size_t i = 0; i < n; ++i) {
        if (in_deg[i] == 0) {
            current_level.push_back(i);
        }
    }

    size_t processed_count = 0;

    // Process DAG level by level
    while (!current_level.empty()) {
        parallel_layers.push_back(current_level);
        processed_count += current_level.size();

        std::vector<size_t> next_level;
        for (size_t u : current_level) {
            for (size_t v : adj[u]) {
                if (--in_deg[v] == 0) {
                    next_level.push_back(v);
                }
            }
        }
        current_level = std::move(next_level);
    }

    if (processed_count != n) {
        throw std::runtime_error("Cycle detected in quantum circuit DAG (violates acyclicity requirement)");
    }

    return parallel_layers;
}

} // namespace qubitos

