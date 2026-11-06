#include "hardware.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <queue>
#include <sstream>
#include <stdexcept>

namespace qubitos {

HardwareGraph HardwareGraph::load_from_json_file(const std::string &filepath) {
  std::ifstream file(filepath);
  if (!file.is_open()) {
    throw std::runtime_error("Cannot open hardware specification file: " +
                             filepath);
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  Json j = Json::parse(buffer.str());
  return load_from_json(j);
}

HardwareGraph HardwareGraph::load_from_json(const Json &j) {
  HardwareGraph hw;
  if (j.contains("name")) {
    hw.name = j["name"].get<std::string>();
  } else {
    hw.name = "Unnamed QPU";
    if (!j.contains("num_qubits")) {
      throw std::runtime_error("hardware.json missing 'num_qubits' field");
    }
    hw.num_qubits = j["num_qubits"].get<size_t>();
    hw.adj.resize(hw.num_qubits);

    if (j.contains("edges")) {
      const auto &edges_arr = j["edges"];
      for (size_t i = 0; i < edges_arr.size(); ++i) {
        const auto &e_val = edges_arr[i];
        HardwareEdge e;
        if (e_val.is_object()) {
          e.a = e_val["a"].get<size_t>();
          e.b = e_val["b"].get<size_t>();
          e.error = e_val["error"].get<double>();
        } else if (e_val.is_array()) {
          e.a = e_val[0].get<size_t>();
          e.b = e_val[1].get<size_t>();
          e.error = e_val[2].get<double>();
        } else {
          throw std::runtime_error("Invalid edge format in hardware.json");
        }

        if (e.a >= hw.num_qubits || e.b >= hw.num_qubits) {
          throw std::runtime_error("Edge refers to qubit out of bounds: (" +
                                   std::to_string(e.a) + ", " +
                                   std::to_string(e.b) + ")");
        }

        hw.edge_list.push_back(e);
        hw.adj[e.a].push_back({e.b, e.error});
        hw.adj[e.b].push_back({e.a, e.error});
      }
    }

    // Sort adjacency lists for deterministic traversal order
    for (size_t i = 0; i < hw.num_qubits; ++i) {
      std::sort(hw.adj[i].begin(), hw.adj[i].end(),
                [](const auto &left, const auto &right) {
                  return left.first < right.first;
                });
    }

    hw.compute_all_pairs_bfs();
    hw.compute_all_pairs_dijkstra();

    return hw;
  }

  void HardwareGraph::compute_all_pairs_bfs() {
    hop_dist.assign(num_qubits, std::vector<size_t>(num_qubits, 1000000));

    for (size_t s = 0; s < num_qubits; ++s) {
      hop_dist[s][s] = 0;
      std::queue<size_t> q;
      q.push(s);

      while (!q.empty()) {
        size_t u = q.front();
        q.pop();

        for (const auto &edge : adj[u]) {
          size_t v = edge.first;
          if (hop_dist[s][v] > hop_dist[s][u] + 1) {
            hop_dist[s][v] = hop_dist[s][u] + 1;
            q.push(v);
          }
        }
      }
    }
  }

  void HardwareGraph::compute_all_pairs_dijkstra() {
    min_error_dist.assign(num_qubits, std::vector<double>(num_qubits, 1e18));

    for (size_t s = 0; s < num_qubits; ++s) {
      min_error_dist[s][s] = 0.0;
      using P = std::pair<double, size_t>;
      std::priority_queue<P, std::vector<P>, std::greater<P>> pq;
      pq.push({0.0, s});

      while (!pq.empty()) {
        auto [d, u] = pq.top();
        pq.pop();

        if (d > min_error_dist[s][u])
          continue;

        for (const auto &edge : adj[u]) {
          size_t v = edge.first;
          double weight = edge.second;
          if (min_error_dist[s][u] + weight < min_error_dist[s][v]) {
            min_error_dist[s][v] = min_error_dist[s][u] + weight;
            pq.push({min_error_dist[s][v], v});
          }
        }
      }
    }
  }

  size_t HardwareGraph::get_hop_distance(size_t p1, size_t p2) const {
    if (p1 >= num_qubits || p2 >= num_qubits)
      return 1000000;
    return hop_dist[p1][p2];
  }

  size_t HardwareGraph::compute_swaps(const std::vector<GatePair> &pairs,
                                      const std::vector<size_t> &mapping)
      const {
    size_t total_swaps = 0;
    for (const auto &pair : pairs) {
      if (pair.q1 >= mapping.size() || pair.q2 >= mapping.size())
        continue;
      size_t p1 = mapping[pair.q1];
      size_t p2 = mapping[pair.q2];
      size_t hops = get_hop_distance(p1, p2);
      if (hops > 1) {
        total_swaps += (hops - 1);
      }
    }
    return total_swaps;
  }

  double HardwareGraph::compute_noise_cost(const std::vector<GatePair> &pairs,
                                           const std::vector<size_t> &mapping)
      const {
    double total_noise = 0.0;
    for (const auto &pair : pairs) {
      if (pair.q1 >= mapping.size() || pair.q2 >= mapping.size())
        continue;
      size_t p1 = mapping[pair.q1];
      size_t p2 = mapping[pair.q2];
      if (p1 < num_qubits && p2 < num_qubits) {
        total_noise += min_error_dist[p1][p2];
      }
    }
    return total_noise;
  }

} // namespace qubitos
