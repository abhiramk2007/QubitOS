#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <filesystem>
#include <iomanip>
#include <algorithm>
#include <unordered_map>
#include "json.hpp"
#include "hardware.hpp"
#include "qasm_parser.hpp"
#include "scheduler.hpp"

namespace fs = std::filesystem;
using namespace qubitos;

struct ManifestEntry {
    std::string file;
    size_t arrival = 0;
    size_t priority = 1;
    size_t deadline = 30;
    bool found_in_manifest = false;
};

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --jobs <dir>          Path to directory containing .qasm files (default: jobs/)\n"
              << "  --hardware <path>     Path to hardware.json file (default: hardware.json)\n"
              << "  --out <path>          Path for output results.json (default: out/results.json)\n"
              << "  --t-layer <int>       Circuit layer execution duration (default: 2)\n"
              << "  --t-swap <int>        SWAP gate execution duration (default: 1)\n"
              << "  --config <path>       Optional JSON configuration file\n"
              << "  --help, -h            Show this help message\n";
}

std::unordered_map<std::string, ManifestEntry> read_manifest(const std::string& manifest_path,
                                                             std::vector<std::string>& manifest_order) {
    std::unordered_map<std::string, ManifestEntry> entries;
    std::ifstream file(manifest_path);
    if (!file.is_open()) {
        return entries;
    }

    std::string line;
    bool is_header = true;
    while (std::getline(file, line)) {
        std::string trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \r\n\t"));
        trimmed.erase(trimmed.find_last_not_of(" \r\n\t") + 1);
        if (trimmed.empty()) continue;

        if (is_header) {
            is_header = false;
            // Check if line looks like header
            if (trimmed.find("file") != std::string::npos || trimmed.find("arrival") != std::string::npos) {
                continue;
            }
        }

        std::stringstream ss(trimmed);
        std::string fname, arr_str, prio_str, ddl_str;
        if (std::getline(ss, fname, ',') &&
            std::getline(ss, arr_str, ',') &&
            std::getline(ss, prio_str, ',') &&
            std::getline(ss, ddl_str, ',')) {
            
            // Trim tokens
            fname.erase(0, fname.find_first_not_of(" \r\n\t"));
            fname.erase(fname.find_last_not_of(" \r\n\t") + 1);
            
            ManifestEntry entry;
            entry.file = fname;
            try { entry.arrival = std::stoul(arr_str); } catch (...) { entry.arrival = 0; }
            try { entry.priority = std::stoul(prio_str); } catch (...) { entry.priority = 1; }
            try { entry.deadline = std::stoul(ddl_str); } catch (...) { entry.deadline = 30; }
            entry.found_in_manifest = true;

            entries[fname] = entry;
            manifest_order.push_back(fname);
        }
    }
    return entries;
}

int main(int argc, char* argv[]) {
    std::string jobs_dir = "jobs/";
    std::string hw_path = "hardware.json";
    std::string out_path = "out/results.json";
    size_t t_layer = 2;
    size_t t_swap = 1;
    std::string config_path = "";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--jobs" && i + 1 < argc) {
            jobs_dir = argv[++i];
        } else if (arg == "--hardware" && i + 1 < argc) {
            hw_path = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            out_path = argv[++i];
        } else if (arg == "--t-layer" && i + 1 < argc) {
            t_layer = std::stoul(argv[++i]);
        } else if (arg == "--t-swap" && i + 1 < argc) {
            t_swap = std::stoul(argv[++i]);
        } else if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    // Optional config.json overrides
    if (!config_path.empty()) {
        try {
            std::ifstream cfile(config_path);
            if (cfile.is_open()) {
                std::stringstream buf;
                buf << cfile.rdbuf();
                Json cj = Json::parse(buf.str());
                if (cj.contains("t_layer")) t_layer = cj["t_layer"].get<size_t>();
                if (cj.contains("t_swap")) t_swap = cj["t_swap"].get<size_t>();
            }
        } catch (const std::exception& e) {
            std::cerr << "Warning: Could not parse config file: " << e.what() << std::endl;
        }
    }

    // 1. Check jobs directory and collect .qasm files
    if (!fs::exists(jobs_dir) || !fs::is_directory(jobs_dir)) {
        std::cout << "No .qasm files found in jobs/. Add exported Composer circuits and rerun." << std::endl;
        return 0;
    }

    std::vector<std::string> qasm_files;
    for (const auto& entry : fs::directory_iterator(jobs_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".qasm") {
            qasm_files.push_back(entry.path().filename().string());
        }
    }

    if (qasm_files.empty()) {
        std::cout << "No .qasm files found in jobs/. Add exported Composer circuits and rerun." << std::endl;
        return 0;
    }

    std::sort(qasm_files.begin(), qasm_files.end());

    // 2. Load hardware specification
    HardwareGraph hw;
    try {
        hw = HardwareGraph::load_from_json_file(hw_path);
    } catch (const std::exception& e) {
        std::cerr << "Error loading hardware specification from '" << hw_path << "': " << e.what() << std::endl;
        return 1;
    }

    // 3. Load manifest if present
    std::string manifest_file = (fs::path(jobs_dir) / "manifest.csv").string();
    std::vector<std::string> manifest_order;
    std::unordered_map<std::string, ManifestEntry> manifest_map;

    if (fs::exists(manifest_file)) {
        manifest_map = read_manifest(manifest_file, manifest_order);
    } else {
        std::cout << "Warning: Manifest not found at '" << manifest_file 
                  << "'. Using default arrival=index, priority=1, deadline=arrival+30." << std::endl;
    }

    // Reorder or index files
    // If manifest exists, files present in manifest follow manifest order first, then remaining files
    std::vector<std::string> ordered_files;
    for (const auto& mfile : manifest_order) {
        if (std::find(qasm_files.begin(), qasm_files.end(), mfile) != qasm_files.end()) {
            ordered_files.push_back(mfile);
        }
    }
    for (const auto& qf : qasm_files) {
        if (std::find(ordered_files.begin(), ordered_files.end(), qf) == ordered_files.end()) {
            ordered_files.push_back(qf);
        }
    }

    // 4. Parse circuits
    std::vector<Job> jobs;
    size_t job_id = 0;

    for (size_t i = 0; i < ordered_files.size(); ++i) {
        const std::string& fname = ordered_files[i];
        std::string full_path = (fs::path(jobs_dir) / fname).string();

        try {
            ParsedCircuit pc = QasmParser::parse_file(full_path);

            Job job;
            job.id = job_id++;
            job.manifest_index = i;
            job.file = fname;
            job.num_qubits = pc.num_qubits;
            job.depth = pc.dag.depth;
            job.two_qubit_pairs = pc.dag.two_qubit_pairs;

            if (manifest_map.find(fname) != manifest_map.end()) {
                const auto& m = manifest_map[fname];
                job.arrival = m.arrival;
                job.priority = m.priority;
                job.deadline = m.deadline;
            } else {
                job.arrival = i;
                job.priority = 1;
                job.deadline = job.arrival + 30;
            }

            // Estimate swaps and runtime with identity mapping
            std::vector<size_t> id_map(job.num_qubits);
            for (size_t q = 0; q < job.num_qubits; ++q) {
                id_map[q] = q;
            }
            job.est_swaps = hw.compute_swaps(job.two_qubit_pairs, id_map);
            job.est_runtime = job.depth * t_layer + job.est_swaps * t_swap;
            job.noise_cost = hw.compute_noise_cost(job.two_qubit_pairs, id_map);

            jobs.push_back(std::move(job));
        } catch (const std::exception& e) {
            std::cerr << "Error parsing file '" << full_path << "': " << e.what() 
                      << "\nSkipping this circuit and continuing." << std::endl;
        }
    }

    if (jobs.empty()) {
        std::cerr << "No valid jobs could be loaded from " << jobs_dir << std::endl;
        return 1;
    }

    // 5. Execute simulation across all 5 policies
    SimulationEngine engine(hw, t_layer, t_swap);
    std::vector<ModeRunResult> runs = engine.run_all_modes(jobs);

    // 6. Print summary table to console
    std::cout << "\n========================================================================================\n";
    std::cout << "                          QubitOS Simulation Results Summary\n";
    std::cout << "========================================================================================\n";
    std::cout << std::left 
              << std::setw(28) << "Scheduling Mode"
              << std::setw(10) << "Avg WT"
              << std::setw(10) << "Avg TAT"
              << std::setw(10) << "Makespan"
              << std::setw(12) << "Util (%)"
              << std::setw(10) << "Misses"
              << std::setw(8)  << "SWAPs"
              << std::setw(10) << "Max Wait"
              << "\n";
    std::cout << "----------------------------------------------------------------------------------------\n";

    for (const auto& run : runs) {
        std::cout << std::left
                  << std::setw(28) << run.name
                  << std::setw(10) << std::fixed << std::setprecision(1) << run.stats.avgWT
                  << std::setw(10) << std::fixed << std::setprecision(1) << run.stats.avgTAT
                  << std::setw(10) << run.stats.makespan
                  << std::setw(12) << std::fixed << std::setprecision(1) << (run.stats.util * 100.0)
                  << std::setw(10) << run.stats.misses
                  << std::setw(8)  << run.stats.swaps
                  << std::setw(10) << run.stats.maxWait
                  << "\n";
    }
    std::cout << "========================================================================================\n\n";

    // 7. Write results.json
    try {
        fs::path out_file_path(out_path);
        if (out_file_path.has_parent_path()) {
            fs::create_directories(out_file_path.parent_path());
        }
        Json result_json = engine.to_results_json(jobs, runs);
        std::ofstream out_file(out_path);
        if (!out_file.is_open()) {
            std::cerr << "Failed to open output file for writing: " << out_path << std::endl;
            return 1;
        }
        out_file << result_json.dump(2) << "\n";
        std::cout << "Successfully generated results JSON: " << out_path << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error writing output JSON to '" << out_path << "': " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

