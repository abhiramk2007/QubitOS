#ifndef QUBITOS_ALLOCATOR_HPP
#define QUBITOS_ALLOCATOR_HPP

#include <vector>
#include <cstddef>
#include "hardware.hpp"

namespace qubitos {

struct AllocationResult {
    bool success = false;
    std::vector<size_t> block; // Sorted physical qubits assigned to the job
    size_t swap_cost = 0;       // Estimated SWAP overhead for this placement
};

// Subgraph allocator for multi-tenant quantum job placement.
// Algorithm:
// - For each free physical qubit s in ascending order (0, 1, 2, ...):
//   - Execute BFS traversing exclusively currently-free physical qubits.
//   - Collect the first n connected free qubits.
//   - If fewer than n free qubits reachable, skip start qubit s.
//   - Sort the collected block in ascending physical order.
//   - Map logical qubit i -> block[i] (i-th smallest physical qubit).
//   - Evaluate swap overhead: sum over 2-qubit gates of max(0, hopDistance(phys(a), phys(b)) - 1).
// - Select the candidate block minimizing swap cost.
// - Deterministic tie-breaking: prioritize the candidate with lowest starting qubit s.
class Allocator {
public:
    static AllocationResult allocate(const HardwareGraph& hw,
                                     const std::vector<bool>& is_free,
                                     size_t n,
                                     const std::vector<GatePair>& pairs);
};

} // namespace qubitos

#endif // QUBITOS_ALLOCATOR_HPP

