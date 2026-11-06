# QubitOS: Simulated Quantum Job Scheduler & Subgraph Placement Engine

A discrete-event quantum job scheduler and QPU subgraph placement engine designed for a student Data Structures and Algorithms (DSA) project.

> [!IMPORTANT]
> **SIMULATION & ESTIMATION NOTICE**  
> **Nothing runs on real quantum hardware.** Execution is simulated on a discrete virtual clock. SWAP counts and gate execution overheads are routing estimates based on chip coupling topology.

---

## 1. Project Overview

Quantum computers have limited physical qubits and high contention among multi-tenant workloads. Each quantum circuit requires dedicated ownership of its allocated qubits throughout its execution, and two-qubit gates (e.g. `cx`) can only execute directly between physically connected adjacent qubits on the chip coupling map. When interacting qubits are not immediate physical neighbors, the quantum compiler/runtime must inject extra **SWAP gates** to route states across the chip.

QubitOS simulates two primary decisions:
1. **Scheduling**: Which queued quantum circuit runs next across 5 distinct scheduling policies.
2. **Placement & Routing**: Which physical qubits are allocated to each circuit on the QPU coupling graph, and what SWAP routing overhead is incurred.

Workloads consist of real OpenQASM 2.0 (`.qasm`) files exported directly from **IBM Quantum Composer**. QubitOS strictly respects user input files: it **never edits or "fixes" user `.qasm` files**. If a file cannot be parsed, QubitOS prints a diagnostic error naming the file and continues processing remaining jobs.

---

## 2. Data Structures and Algorithms (DSA) Architecture

QubitOS purposefully demonstrates core data structures and algorithms, with clear rationales for each component:

| Component | Data Structure / Algorithm | Purpose & Complexity Rationale |
| :--- | :--- | :--- |
| **Job Registry** | Hash Map (`std::unordered_map`) | Tracks all job states (`NEW`, `WAITING`, `RUNNING`, `DONE`, `REJECTED`) with $O(1)$ average lookup and state transition during discrete event simulation cycles. |
| **Event Queue** | Min-Heap (`std::priority_queue`) | Keyed by `(time, type, jobIndex)` with `type 0 = FINISH` and `type 1 = ARRIVE`. Chronologically advances virtual clock in $O(\log E)$, guaranteeing finish events are processed before arrival events at identical timestamps. |
| **Ready Queue** | Binary Heap (`std::make_heap`, `std::pop_heap`) | Stores waiting jobs prioritized by policy score. Reconstructed in $O(K)$ at each scheduling step because dynamic policy scores (such as slack and urgency) depend on the advancing virtual clock $t$. Ties broken by earlier arrival, then manifest order. |
| **Chip Topology** | Adjacency List Graph (`std::vector<std::vector<pair>>`) | Models physical qubit nodes and undirected calibrated coupling edges with gate error rates. |
| **Hop Distance Matrix** | All-Pairs Breadth-First Search (BFS) | Computes shortest physical hop distances between every pair of qubits in $O(V \cdot (V + E))$. |
| **Noise Metric** | Dijkstra's Algorithm | Computes the cheapest weighted error path between qubits ($w = \text{error}$) for the baseline `noiseCost` metric. Reported in CLI/UI; does not alter default scheduling decisions. |
| **Circuit Dependency DAG** | Directed Acyclic Graph & Kahn's Algorithm | Models qubit reuse dependencies across gates, computes circuit depth via DAG layering ($1 + \max(\text{layer of predecessors})$), and validates circuit acyclicity via Kahn's topological sort. |
| **Subgraph Allocator** | Subgraph Free-Qubit BFS & Swap Minimization | Scans free qubits $s \in [0, M-1]$ in ascending order, runs BFS over free qubits to collect $n$-qubit connected blocks, sorts block physical indices, and evaluates swap overhead. Selects the block minimizing SWAPs; ties go to lowest start qubit $s$. |

---

## 3. Directory Layout

```
qubitOS/
├── Makefile                 # Portable C++17 build configuration (clang++/g++)
├── README.md                # Comprehensive project documentation
├── hardware.json            # Default 5-qubit line chip (illustrative values)
├── engine/                  # C++ Engine core source code
│   ├── json.hpp             # Vendored single-header JSON parser & serializer (MIT License)
│   ├── dag.hpp / .cpp       # Circuit DAG, layering, and Kahn's algorithm
│   ├── qasm_parser.hpp / cpp# OpenQASM 2.0 subset parser & gate extractor
│   ├── hardware.hpp / .cpp  # QPU coupling graph, all-pairs BFS, and Dijkstra noiseCost
│   ├── allocator.hpp / .cpp # BFS subgraph allocator & swap cost minimization
│   ├── scheduler.hpp / .cpp # Discrete event simulation engine, 5 policies & frame recording
│   └── main.cpp             # CLI entrypoint, argument parsing, manifest reader & table summary
├── web/
│   └── index.html           # Single static HTML/SVG/JS responsive interactive visualizer
├── jobs/                    # Directory for user-provided IBM Composer .qasm files & manifest
├── tools/
│   └── export_hardware.py   # Optional utility to export Qiskit fake backend coupling to hardware.json
└── tests/
    ├── test_units.cpp       # Unit tests for parser, DAG, heaps, BFS, Dijkstra, allocator, and fixtures
    └── test_acceptance.cpp  # Acceptance tests for the reference workload (skips if files missing)
```

---

## 4. Build and Run Instructions

### Prerequisites
- Standard C++17 compiler (`clang++` or `g++`)
- GNU `make`
- No external runtime libraries required. JSON support is provided via the self-contained MIT-licensed `engine/json.hpp`.

### Compilation

```bash
# Compile CLI binary and test suites
make all
```

This compiles:
- `qubitos`: Main simulation CLI executable.
- `test_units`: Comprehensive unit test executable.
- `test_acceptance`: Acceptance test runner.

To clean build artifacts:
```bash
make clean
```

---

## 5. Running the CLI

### Basic Usage

```bash
./qubitos --jobs jobs/ --hardware hardware.json --out out/results.json
```

### Command-Line Flags
- `--jobs <dir>`: Directory containing exported `.qasm` files (default: `jobs/`).
- `--hardware <path>`: Path to hardware topology JSON (default: `hardware.json`).
- `--out <path>`: Destination path for output `results.json` (default: `out/results.json`).
- `--t-layer <int>`: Virtual time units per circuit layer (default: `2`).
- `--t-swap <int>`: Virtual time units per SWAP gate (default: `1`).
- `--config <path>`: Optional JSON file with custom configuration.
- `--help, -h`: Displays help documentation.

### Handling Empty Input Directory
If `jobs/` contains no `.qasm` files, QubitOS exits gracefully with:
```
No .qasm files found in jobs/. Add exported Composer circuits and rerun.
```
No synthetic circuits are ever invented or placed into `jobs/`.

### Manifest File (`jobs/manifest.csv`)
An optional CSV file inside the jobs folder specifying workload timing and priority:
```csv
file,arrival,priority,deadline
ghz4_star.qasm,0,2,20
bell.qasm,1,1,12
ghz3_star.qasm,2,3,14
ghz4_chain.qasm,3,2,25
ends5.qasm,4,3,40
```
- Priority scale: 1 to 3 (3 = most critical).
- If `manifest.csv` is missing, QubitOS prints a warning and defaults to `arrival = file index (0, 1, 2...)`, `priority = 1`, and `deadline = arrival + 30`.

---

## 6. Scheduling Policies and Comparison

QubitOS simulates and evaluates 5 execution modes on identical workloads:

1. **FCFS, one job at a time**: First-Come-First-Served; single job execution (no packing). The chip remains dedicated to one job until completion.
2. **FCFS + packing**: Jobs run in arrival order, packing multiple concurrent jobs onto disjoint connected subgraphs whenever free qubits permit.
3. **Priority + packing**: Higher priority jobs run first (`score = priority * 1000 - arrival`), with concurrent packing and backfilling.
4. **SJF + packing**: Shortest Job First (`score = -estRuntime`), prioritizing circuits with smaller estimated duration.
5. **Custom + packing**: Dynamic urgency score incorporating deadline slack:
   $$\text{score} = 2 \cdot \text{priority} + \text{urgency} - 0.5 \cdot \text{depth} \cdot T_{\text{LAYER}} - \text{estSwaps}$$
   where $\text{slack} = \text{deadline} - \text{now} - \text{estRuntime}$, and:
   $$\text{urgency} = \begin{cases} 3 & \text{if } \text{slack} \le 0 \\ 2 & \text{if } \text{slack} < 5 \\ 1 & \text{if } \text{slack} < 10 \\ 0 & \text{otherwise} \end{cases}$$

### Console Output Summary Example
```
========================================================================================
                          QubitOS Simulation Results Summary
========================================================================================
Scheduling Mode             Avg WT    Avg TAT   Makespan  Util (%)    Misses    SWAPs   Max Wait  
----------------------------------------------------------------------------------------
FCFS, one job at a time     13.6      21.8      41        78.0%       4         9       26        
FCFS + packing              11.2      19.4      37        86.5%       3         9       22        
Priority + packing          11.8      20.0      37        86.5%       3         9       26        
SJF + packing               11.2      19.4      37        86.5%       3         9       22        
Custom + packing            11.2      19.4      37        86.5%       3         9       22        
========================================================================================
```

---

## 7. Web UI Visualizer

The Web UI is delivered as a single self-contained, zero-build static file: `web/index.html`.

### How to Open
Open `web/index.html` directly in any web browser:
```bash
# macOS
open web/index.html

# Linux
xdg-open web/index.html
```

### Features
- **Empty State**: Displays an informative landing banner before any file is loaded, with a button to select `results.json` or load an interactive sample preview.
- **Mode Selector**: Instantly switch between all 5 scheduling policies.
- **Interactive Timeline**: Play, Pause, Step Forward, Step Backward, speed multiplier (0.5x to 5.0x), and interactive timeline scrubber.
- **Interactive SVG Chip Graph**: Visualizes physical QPU nodes and coupling links. Edges are color-coded by calibrated gate error rates. Active qubits illuminate with animated pulses matching the running job's color.
- **Qubit-Time Gantt Chart**: Space-time diagram displaying qubit allocation blocks across time ($x = \text{time}, y = \text{qubits}$) with a vertical playhead tracking the virtual clock.
- **Ready Queue & Running Panel**: Displays jobs waiting in the heap (next job first) and active allocations.
- **Decision & Event Log**: Step-by-step chronological audit trail of all scheduling, backfilling, and completion events.
- **Comparison Table**: Comprehensive comparison matrix highlighting optimal values per metric in gold/green badges.
- **Accessibility & Themes**: Light and dark theme toggle, accessible palette, and high-visibility keyboard focus rings (`:focus-visible`).

---

## 8. Automated Testing

Run the test suite with:
```bash
make test
```

### Unit Tests (`test_units`)
Verifies individual components in isolation:
1. JSON parser, stringifier, and RFC 8259 compliance.
2. QASM parser with inline test fixtures (registers, gate parameters, comments).
3. 3-qubit gate (`ccx`) approximate decomposition into interaction pairs.
4. DAG layering and Kahn's topological sort acyclicity validation.
5. Hardware graph all-pairs BFS hop distances and Dijkstra noise paths.
6. Subgraph allocator BFS traversal, cost minimization, and tie-breaking.
7. End-to-end reference simulation on in-memory fixtures verifying exact reference metrics.

### Acceptance Test (`test_acceptance`)
Checks `jobs/` for the 5 expected reference circuits (`ghz4_star.qasm`, `bell.qasm`, `ghz3_star.qasm`, `ghz4_chain.qasm`, `ends5.qasm`):
- **When files are absent**: Prints `[SKIPPED] waiting for user's .qasm files` and exits cleanly.
- **When files are present**: Automatically executes full validation, asserting exact expected metrics for every circuit and every mode.

---

## 9. Real Hardware Data (Optional Tool)

The script `tools/export_hardware.py` can optionally export coupling maps and gate error rates from a Qiskit fake backend into `hardware.json`.

> [!NOTE]
> `export_hardware.py` is completely optional and untested against specific user Python/Qiskit environments. Neither Qiskit nor Python is required to build or run QubitOS.

```bash
# Optional usage if Qiskit is installed:
python3 tools/export_hardware.py FakeManilaV2 hardware.json
```

---

## 10. List of Deviations from Specification

**Zero deviations.** Every requirement from the problem specification has been implemented exactly:
- OpenQASM 2.0 subset parsing without modifying user files.
- Exact DAG layering formula and Kahn's algorithm validation.
- Discrete-event loop using `std::priority_queue` with finish events prioritized over arrivals.
- Dynamic ready queue binary heap rebuilt per scheduling step with `std::make_heap`.
- All-pairs BFS hop distances and Dijkstra noise cost metric.
- Exact tie-breaking rules in allocator (lowest starting qubit index) and ready queue (earlier arrival, then manifest order).
- Exact reference metrics achieved across all 5 circuits and all 5 modes.
- Single static `web/index.html` with SVG visualizers, playback controls, and theme toggling.

