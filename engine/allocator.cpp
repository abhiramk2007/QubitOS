#include "allocator.hpp"
#include <queue>
#include <algorithm>
#include <cstdint>

namespace qubitos {

AllocationResult Allocator::allocate(const HardwareGraph& hw,
                                     const std::vector<bool>& is_free,
                                     size_t n,
                                     const std::vector<GatePair>& pairs) {
    if (n == 0 || n > hw.num_qubits) {
        return {false, {}, 0};
    }

    size_t free_count = 0;
    for (size_t i = 0; i < hw.num_qubits; ++i) {
        if (is_free[i]) free_count++;
    }
    if (free_count < n) {
        return {false, {}, 0};
    }

    bool found = false;
    size_t best_cost = SIZE_MAX;
    std::vector<size_t> best_block;

    // Scan free qubits in ascending order (0, 1, 2, ...)
    for (size_t s = 0; s < hw.num_qubits; ++s) {
        if (!is_free[s]) continue;

        // BFS traversal over free subgraph starting at s
        std::vector<bool> visited(hw.num_qubits, false);
        std::queue<size_t> q;
        std::vector<size_t> candidate_block;

        visited[s] = true;
        candidate_block.push_back(s);
        q.push(s);

        while (!q.empty() && candidate_block.size() < n) {
            size_t u = q.front();
            q.pop();

            for (const auto& edge : hw.adj[u]) {
                size_t v = edge.first;
                if (is_free[v] && !visited[v]) {
                    visited[v] = true;
                    candidate_block.push_back(v);
                    q.push(v);
                    if (candidate_block.size() == n) {
                        break;
                    }
                }
            }
        }

        // If connected component has fewer than n free qubits, skip start node s
        if (candidate_block.size() < n) {
            continue;
        }

        // Canonical placement: sort physical qubits in ascending order
        std::sort(candidate_block.begin(), candidate_block.end());

        // Evaluate SWAP routing cost under this placement mapping
        size_t cost = hw.compute_swaps(pairs, candidate_block);

        // Retain candidate minimizing swap cost.
        // Ties strictly favor the lowest starting qubit s (earlier iteration index).
        if (!found || cost < best_cost) {
            found = true;
            best_cost = cost;
            best_block = candidate_block;
        }
    }

    if (found) {
        return {true, best_block, best_cost};
    }

    return {false, {}, 0};
}

} // namespace qubitos

