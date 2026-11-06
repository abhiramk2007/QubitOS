#ifndef QUBITOS_SCHEDULER_HPP
#define QUBITOS_SCHEDULER_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "json.hpp"
#include "hardware.hpp"
#include "dag.hpp"
#include "allocator.hpp"

namespace qubitos {

enum class JobState {
    NEW,
    WAITING,
    RUNNING,
    DONE,
    REJECTED
};

struct Job {
    size_t id = 0;
    size_t manifest_index = 0;
    std::string file;
    size_t num_qubits = 0;
    size_t depth = 0;
    size_t arrival = 0;
    size_t priority = 1; // 1 to 3 (3 = highest)
    size_t deadline = 0;

    std::vector<GatePair> two_qubit_pairs;

    // Load-time estimations (using identity mapping on QPU)
    size_t est_swaps = 0;
    size_t est_runtime = 0;
    double noise_cost = 0.0;
};

struct JobExecutionRecord {
    size_t id = 0;
    JobState state = JobState::NEW;
    long long start_time = -1;
    long long finish_time = -1;
    long long wait_time = 0;
    long long turnaround_time = 0;
    size_t actual_swaps = 0;
    bool missed_deadline = false;
    std::vector<size_t> region;
};

enum class SchedulingPolicy {
    FCFS,
    PRIORITY,
    SJF,
    CUSTOM
};

struct ModeConfig {
    std::string name;
    SchedulingPolicy policy;
    bool packing = true;
};

struct SimFrame {
    size_t t = 0;
    std::vector<std::string> log;
    std::vector<size_t> waiting; // Ready queue IDs (next job to run first)
    struct RunningEntry {
        size_t id;
        std::vector<size_t> qubits;
        size_t start;
        size_t end;
    };
    std::vector<RunningEntry> running;
    std::vector<size_t> done;
};

struct RunStats {
    double avgWT = 0.0;
    double avgTAT = 0.0;
    size_t makespan = 0;
    double util = 0.0;
    size_t misses = 0;
    size_t swaps = 0;
    size_t maxWait = 0;
};

struct ModeRunResult {
    std::string name;
    RunStats stats;
    std::vector<JobExecutionRecord> results;
    std::vector<SimFrame> frames;
};

class SimulationEngine {
public:
    SimulationEngine(const HardwareGraph& hw, size_t t_layer = 2, size_t t_swap = 1);

    // Runs a specific mode simulation on the provided jobs
    ModeRunResult run_mode(const ModeConfig& mode, const std::vector<Job>& jobs);

    // Runs all 5 standard modes
    std::vector<ModeRunResult> run_all_modes(const std::vector<Job>& jobs);

    // Exports simulation outputs to results.json format
    Json to_results_json(const std::vector<Job>& jobs, const std::vector<ModeRunResult>& runs) const;

private:
    HardwareGraph hw_;
    size_t t_layer_ = 2;
    size_t t_swap_ = 1;

    // Evaluates dynamic policy score for a job at virtual clock timestamp 'now'
    static double compute_policy_score(SchedulingPolicy policy,
                                       const Job& job,
                                       size_t now,
                                       size_t t_layer);
};

} // namespace qubitos

#endif // QUBITOS_SCHEDULER_HPP

