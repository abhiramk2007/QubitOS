#ifndef QUBITOS_DAG_HPP
#define QUBITOS_DAG_HPP

#include <vector>
#include <string>
#include <utility>

namespace qubitos {

// Represents a quantum gate within a circuit DAG
struct Gate {
    size_t id = 0;
    std::string name;
    std::vector<size_t> qubits; // Global qubit indices
    size_t layer = 0;           // Layer computed via dependency layering
    bool is_approximate = false;// Flagged true for decomposed 3-qubit gates (e.g., ccx)
};

// Represents a 2-qubit interaction pair for routing estimation
struct GatePair {
    size_t q1 = 0;
    size_t q2 = 0;
    bool approximate = false;
};

// Directed Acyclic Graph (DAG) for circuit gate dependencies
// Uses adjacency list and Kahn's algorithm for validation and parallel layering
class CircuitDAG {
public:
    size_t num_qubits = 0;
    std::vector<Gate> gates;
    std::vector<std::vector<size_t>> adj;      // adj[u] = list of successor gate IDs
    std::vector<std::vector<size_t>> in_edges; // in_edges[v] = list of predecessor gate IDs
    std::vector<GatePair> two_qubit_pairs;     // Ordered list of 2-qubit interactions
    size_t depth = 0;

    CircuitDAG() = default;

    // Adds a gate, links dependencies from previous gates on its qubits,
    // updates layers, and appends 2-qubit interaction pairs.
    void add_gate(const std::string& name, const std::vector<size_t>& qubits);

    // Finalizes the DAG: executes Kahn's algorithm to validate acyclicity
    // and returns the parallel execution layers (each layer is a vector of gate IDs).
    std::vector<std::vector<size_t>> run_kahns_algorithm();

private:
    std::vector<int> last_gate_on_qubit; // Tracks most recent gate ID for each qubit
};

} // namespace qubitos

#endif // QUBITOS_DAG_HPP

