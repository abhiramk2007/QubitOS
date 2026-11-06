#include "scheduler.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>

namespace qubitos {

SimulationEngine::SimulationEngine(const HardwareGraph& hw, size_t t_layer, size_t t_swap)
    : hw_(hw), t_layer_(t_layer), t_swap_(t_swap) {}

double SimulationEngine::compute_policy_score(SchedulingPolicy policy,
                                              const Job& job,
                                              size_t now,
                                              size_t t_layer) {
    switch (policy) {
        case SchedulingPolicy::FCFS:
            return -static_cast<double>(job.arrival);

        case SchedulingPolicy::PRIORITY:
            return static_cast<double>(job.priority) * 1000.0 - static_cast<double>(job.arrival);

        case SchedulingPolicy::SJF:
            return -static_cast<double>(job.est_runtime);

        case SchedulingPolicy::CUSTOM: {
            long long slack = static_cast<long long>(job.deadline) - 
                              static_cast<long long>(now) - 
                              static_cast<long long>(job.est_runtime);
            double urgency = 0.0;
            if (slack <= 0) {
                urgency = 3.0;
            } else if (slack < 5) {
                urgency = 2.0;
            } else if (slack < 10) {
                urgency = 1.0;
            } else {
                urgency = 0.0;
            }
            return 2.0 * static_cast<double>(job.priority) + urgency - 
                   0.5 * static_cast<double>(job.depth * t_layer) - 
                   static_cast<double>(job.est_swaps);
        }
    }
    return 0.0;
}

struct SimEvent {
    size_t time;
    int type; // 0 = finish, 1 = arrive (finishes processed before arrivals at identical timestamp)
    size_t job_index;
};

struct EventMinComparator {
    bool operator()(const SimEvent& a, const SimEvent& b) const {
        if (a.time != b.time) return a.time > b.time;
        if (a.type != b.type) return a.type > b.type;
        return a.job_index > b.job_index;
    }
};

ModeRunResult SimulationEngine::run_mode(const ModeConfig& mode, const std::vector<Job>& jobs) {
    ModeRunResult result;
    result.name = mode.name;

    // Hash map (unordered_map): job registry id -> record
    // Provides O(1) average lookup/update during discrete event processing
    std::unordered_map<size_t, JobExecutionRecord> registry;
    std::unordered_map<size_t, const Job*> jobs_by_id;

    // Min-heap (priority_queue) for events, keyed (time, type, jobIndex)
    // Finishes (type 0) processed before arrivals (type 1) at identical timestamp
    std::priority_queue<SimEvent, std::vector<SimEvent>, EventMinComparator> event_queue;

    std::vector<bool> is_free(hw_.num_qubits, true);
    std::vector<size_t> ready_queue; // Binary heap rebuilt per scheduling step
    std::vector<size_t> running_jobs;
    std::vector<size_t> done_jobs;

    for (const auto& job : jobs) {
        jobs_by_id[job.id] = &job;
        JobExecutionRecord rec;
        rec.id = job.id;

        if (job.num_qubits > hw_.num_qubits) {
            rec.state = JobState::REJECTED;
            rec.missed_deadline = true;
            registry[job.id] = rec;
            std::cerr << "Warning: Job '" << job.file << "' requires " << job.num_qubits 
                      << " qubits, which exceeds chip capacity (" << hw_.num_qubits 
                      << "). State marked REJECTED." << std::endl;
        } else {
            rec.state = JobState::NEW;
            registry[job.id] = rec;
            event_queue.push({job.arrival, 1, job.id});
        }
    }

    if (event_queue.empty()) {
        return result;
    }

    while (!event_queue.empty()) {
        size_t now = event_queue.top().time;
        std::vector<std::string> step_log;

        // 1. Process all discrete events arriving/finishing at current timestamp 'now'
        while (!event_queue.empty() && event_queue.top().time == now) {
            SimEvent ev = event_queue.top();
            event_queue.pop();

            const Job* job_ptr = jobs_by_id[ev.job_index];
            JobExecutionRecord& rec = registry[ev.job_index];

            if (ev.type == 0) {
                // FINISH event
                rec.state = JobState::DONE;
                for (size_t q : rec.region) {
                    is_free[q] = true;
                }
                auto it = std::find(running_jobs.begin(), running_jobs.end(), ev.job_index);
                if (it != running_jobs.end()) {
                    running_jobs.erase(it);
                }
                done_jobs.push_back(ev.job_index);
                step_log.push_back("Job " + job_ptr->file + " finished execution at virtual clock t=" + std::to_string(now));
            } else if (ev.type == 1) {
                // ARRIVE event
                rec.state = JobState::WAITING;
                ready_queue.push_back(ev.job_index);
                step_log.push_back("Job " + job_ptr->file + " arrived at virtual clock t=" + std::to_string(now));
            }
        }

        // 2. Scheduling step: repeatedly take best job from heap and try to allocate it
        // If mode is non-packing (Mode 1), only 1 job can be active on QPU at any time
        std::vector<size_t> skipped_jobs;

        while (!ready_queue.empty()) {
            if (!mode.packing && !running_jobs.empty()) {
                break;
            }

            // Binary heap comparator for ready queue:
            // High score runs first; ties broken by earlier arrival, then manifest order
            auto comp = [&](size_t a_id, size_t b_id) {
                const auto& ja = *jobs_by_id[a_id];
                const auto& jb = *jobs_by_id[b_id];
                double sa = compute_policy_score(mode.policy, ja, now, t_layer_);
                double sb = compute_policy_score(mode.policy, jb, now, t_layer_);
                if (std::abs(sa - sb) > 1e-9) return sa < sb;
                if (ja.arrival != jb.arrival) return ja.arrival > jb.arrival;
                return ja.manifest_index > jb.manifest_index;
            };

            // Rebuild heap each scheduling step with make_heap to dynamically update time-dependent scores
            std::make_heap(ready_queue.begin(), ready_queue.end(), comp);
            std::pop_heap(ready_queue.begin(), ready_queue.end(), comp);
            size_t cand_id = ready_queue.back();
            ready_queue.pop_back();

            const Job* c_job = jobs_by_id[cand_id];
            AllocationResult alloc = Allocator::allocate(hw_, is_free, c_job->num_qubits, c_job->two_qubit_pairs);

            if (alloc.success) {
                // Allocation succeeded: reserve qubits and schedule finish event
                JobExecutionRecord& rec = registry[cand_id];
                rec.state = JobState::RUNNING;
                rec.start_time = now;
                rec.actual_swaps = alloc.swap_cost;
                rec.region = alloc.block;
                size_t actual_runtime = c_job->depth * t_layer_ + alloc.swap_cost * t_swap_;
                rec.finish_time = now + actual_runtime;
                rec.wait_time = rec.start_time - static_cast<long long>(c_job->arrival);
                rec.turnaround_time = rec.finish_time - static_cast<long long>(c_job->arrival);
                rec.missed_deadline = (rec.finish_time > static_cast<long long>(c_job->deadline));

                for (size_t q : alloc.block) {
                    is_free[q] = false;
                }
                running_jobs.push_back(cand_id);

                event_queue.push({static_cast<size_t>(rec.finish_time), 0, cand_id});

                std::string q_str = "[";
                for (size_t i = 0; i < alloc.block.size(); ++i) {
                    q_str += std::to_string(alloc.block[i]);
                    if (i + 1 < alloc.block.size()) q_str += ",";
                }
                q_str += "]";
                step_log.push_back("Job " + c_job->file + " allocated on qubits " + q_str + 
                                   " (swaps=" + std::to_string(alloc.swap_cost) + 
                                   ", runtime=" + std::to_string(actual_runtime) + ")");
            } else {
                // Backfilling: job cannot fit on currently free qubits; skip for this step
                skipped_jobs.push_back(cand_id);
            }
        }

        // Put skipped jobs back into ready queue
        for (size_t sid : skipped_jobs) {
            ready_queue.push_back(sid);
        }

        // 3. Record simulation frame at event timestamp 'now', after scheduling
        SimFrame frame;
        frame.t = now;
        frame.log = step_log;

        // Waiting queue ordered with next job to run first
        auto next_first_comp = [&](size_t a_id, size_t b_id) {
            const auto& ja = *jobs_by_id[a_id];
            const auto& jb = *jobs_by_id[b_id];
            double sa = compute_policy_score(mode.policy, ja, now, t_layer_);
            double sb = compute_policy_score(mode.policy, jb, now, t_layer_);
            if (std::abs(sa - sb) > 1e-9) return sa > sb;
            if (ja.arrival != jb.arrival) return ja.arrival < jb.arrival;
            return ja.manifest_index < jb.manifest_index;
        };
        std::vector<size_t> sorted_waiting = ready_queue;
        std::sort(sorted_waiting.begin(), sorted_waiting.end(), next_first_comp);
        frame.waiting = sorted_waiting;

        for (size_t rid : running_jobs) {
            const auto& rec = registry[rid];
            frame.running.push_back({rid, rec.region, static_cast<size_t>(rec.start_time), static_cast<size_t>(rec.finish_time)});
        }
        frame.done = done_jobs;
        result.frames.push_back(std::move(frame));
    }

    // Populate final results array and compute aggregate metrics
    double sum_wt = 0.0;
    double sum_tat = 0.0;
    size_t total_swaps = 0;
    size_t deadline_misses = 0;
    size_t max_wt = 0;
    size_t makespan = 0;
    size_t total_qubit_time = 0;
    size_t completed_count = 0;

    for (const auto& job : jobs) {
        const auto& rec = registry[job.id];
        result.results.push_back(rec);

        if (rec.state == JobState::DONE) {
            completed_count++;
            sum_wt += static_cast<double>(rec.wait_time);
            sum_tat += static_cast<double>(rec.turnaround_time);
            total_swaps += rec.actual_swaps;
            if (rec.missed_deadline) deadline_misses++;
            if (static_cast<size_t>(rec.wait_time) > max_wt) {
                max_wt = static_cast<size_t>(rec.wait_time);
            }
            if (static_cast<size_t>(rec.finish_time) > makespan) {
                makespan = static_cast<size_t>(rec.finish_time);
            }
            total_qubit_time += job.num_qubits * (rec.finish_time - rec.start_time);
        }
    }

    result.stats.makespan = makespan;
    result.stats.misses = deadline_misses;
    result.stats.swaps = total_swaps;
    result.stats.maxWait = max_wt;

    if (completed_count > 0) {
        result.stats.avgWT = sum_wt / static_cast<double>(completed_count);
        result.stats.avgTAT = sum_tat / static_cast<double>(completed_count);
    }
    if (makespan > 0 && hw_.num_qubits > 0) {
        result.stats.util = static_cast<double>(total_qubit_time) / 
                            (static_cast<double>(hw_.num_qubits) * static_cast<double>(makespan));
    }

    return result;
}

std::vector<ModeRunResult> SimulationEngine::run_all_modes(const std::vector<Job>& jobs) {
    std::vector<ModeConfig> modes = {
        {"FCFS, one job at a time", SchedulingPolicy::FCFS, false},
        {"FCFS + packing", SchedulingPolicy::FCFS, true},
        {"Priority + packing", SchedulingPolicy::PRIORITY, true},
        {"SJF + packing", SchedulingPolicy::SJF, true},
        {"Custom + packing", SchedulingPolicy::CUSTOM, true}
    };

    std::vector<ModeRunResult> results;
    for (const auto& mode : modes) {
        results.push_back(run_mode(mode, jobs));
    }
    return results;
}

Json SimulationEngine::to_results_json(const std::vector<Job>& jobs, const std::vector<ModeRunResult>& runs) const {
    Json root = Json::object();

    // 1. Hardware section
    Json hw_obj = Json::object();
    hw_obj["name"] = hw_.name;
    hw_obj["num_qubits"] = static_cast<int>(hw_.num_qubits);
    hw_obj["n"] = static_cast<int>(hw_.num_qubits);

    Json edges_arr = Json::array();
    for (const auto& e : hw_.edge_list) {
        Json edge_entry = Json::array();
        edge_entry.push_back(static_cast<int>(e.a));
        edge_entry.push_back(static_cast<int>(e.b));
        edge_entry.push_back(e.error);
        edges_arr.push_back(edge_entry);
    }
    hw_obj["edges"] = edges_arr;
    root["hardware"] = hw_obj;

    // 2. Jobs section
    Json jobs_arr = Json::array();
    for (const auto& j : jobs) {
        Json j_obj = Json::object();
        j_obj["id"] = static_cast<int>(j.id);
        j_obj["file"] = j.file;
        j_obj["qubits"] = static_cast<int>(j.num_qubits);
        j_obj["depth"] = static_cast<int>(j.depth);
        j_obj["arrival"] = static_cast<int>(j.arrival);
        j_obj["priority"] = static_cast<int>(j.priority);
        j_obj["deadline"] = static_cast<int>(j.deadline);
        j_obj["est"] = static_cast<int>(j.est_runtime);
        j_obj["noiseCost"] = j.noise_cost;
        jobs_arr.push_back(j_obj);
    }
    root["jobs"] = jobs_arr;

    // 3. Runs section
    Json runs_arr = Json::array();
    for (const auto& run : runs) {
        Json run_obj = Json::object();
        run_obj["name"] = run.name;

        Json stats_obj = Json::object();
        stats_obj["avgWT"] = run.stats.avgWT;
        stats_obj["avgTAT"] = run.stats.avgTAT;
        stats_obj["makespan"] = static_cast<int>(run.stats.makespan);
        stats_obj["util"] = run.stats.util;
        stats_obj["misses"] = static_cast<int>(run.stats.misses);
        stats_obj["swaps"] = static_cast<int>(run.stats.swaps);
        stats_obj["maxWait"] = static_cast<int>(run.stats.maxWait);
        run_obj["stats"] = stats_obj;

        Json results_arr = Json::array();
        for (const auto& res : run.results) {
            Json res_obj = Json::object();
            res_obj["id"] = static_cast<int>(res.id);
            res_obj["start"] = static_cast<int>(res.start_time);
            res_obj["finish"] = static_cast<int>(res.finish_time);
            res_obj["wt"] = static_cast<int>(res.wait_time);
            res_obj["tat"] = static_cast<int>(res.turnaround_time);
            res_obj["swaps"] = static_cast<int>(res.actual_swaps);
            res_obj["missed"] = res.missed_deadline;

            Json reg_arr = Json::array();
            for (size_t q : res.region) {
                reg_arr.push_back(static_cast<int>(q));
            }
            res_obj["region"] = reg_arr;
            results_arr.push_back(res_obj);
        }
        run_obj["results"] = results_arr;

        Json frames_arr = Json::array();
        for (const auto& frame : run.frames) {
            Json f_obj = Json::object();
            f_obj["t"] = static_cast<int>(frame.t);

            Json log_arr = Json::array();
            for (const auto& l : frame.log) {
                log_arr.push_back(l);
            }
            f_obj["log"] = log_arr;

            Json wait_arr = Json::array();
            for (size_t wid : frame.waiting) {
                wait_arr.push_back(static_cast<int>(wid));
            }
            f_obj["waiting"] = wait_arr;

            Json run_arr = Json::array();
            for (const auto& ren : frame.running) {
                Json ro = Json::object();
                ro["id"] = static_cast<int>(ren.id);
                ro["start"] = static_cast<int>(ren.start);
                ro["end"] = static_cast<int>(ren.end);
                Json qb = Json::array();
                for (size_t q : ren.qubits) {
                    qb.push_back(static_cast<int>(q));
                }
                ro["qubits"] = qb;
                run_arr.push_back(ro);
            }
            f_obj["running"] = run_arr;

            Json done_arr = Json::array();
            for (size_t did : frame.done) {
                done_arr.push_back(static_cast<int>(did));
            }
            f_obj["done"] = done_arr;

            frames_arr.push_back(f_obj);
        }
        run_obj["frames"] = frames_arr;

        runs_arr.push_back(run_obj);
    }
    root["runs"] = runs_arr;

    return root;
}

} // namespace qubitos

