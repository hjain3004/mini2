#include "config/ConfigManager.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace mini2 {

namespace {

std::string trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

std::vector<std::string> split(const std::string& s, char delim) {
  std::vector<std::string> out;
  std::string cur;
  std::istringstream iss(s);
  while (std::getline(iss, cur, delim)) out.push_back(trim(cur));
  return out;
}

// "B:127.0.0.1:50052" -> NeighborEntry
NeighborEntry parse_neighbor(const std::string& token) {
  auto parts = split(token, ':');
  if (parts.size() != 3) {
    throw std::runtime_error("neighbor must be id:host:port, got: " + token);
  }
  NeighborEntry n;
  n.id = parts[0];
  n.host = parts[1];
  n.port = static_cast<uint16_t>(std::stoi(parts[2]));
  return n;
}

}  // namespace

Config ConfigManager::load(const std::string& path) {
  std::ifstream f(path);
  if (!f.is_open()) {
    throw std::runtime_error("cannot open config file: " + path);
  }

  Config c;
  std::string line;
  int lineno = 0;
  while (std::getline(f, line)) {
    ++lineno;
    auto trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#') continue;

    auto eq = trimmed.find('=');
    if (eq == std::string::npos) {
      throw std::runtime_error("config line " + std::to_string(lineno) +
                               " missing '=': " + line);
    }
    auto key = trim(trimmed.substr(0, eq));
    auto val = trim(trimmed.substr(eq + 1));

    if (key == "node_id")              c.node_id = val;
    else if (key == "role")            c.role = val;
    else if (key == "language")        c.language = val;
    else if (key == "host")            c.host = val;
    else if (key == "port")            c.port = static_cast<uint16_t>(std::stoi(val));
    else if (key == "dataset_path")    c.dataset_path = val;
    else if (key == "overlay")         c.overlay = val;
    else if (key == "neighbors") {
      if (!val.empty()) {
        for (auto& tok : split(val, ',')) {
          if (!tok.empty()) c.neighbors.push_back(parse_neighbor(tok));
        }
      }
    }
    else if (key == "default_chunk_records") c.default_chunk_records = std::stoi(val);
    else if (key == "max_chunk_bytes")       c.max_chunk_bytes = std::stoll(val);
    else if (key == "adaptive_chunking")     c.adaptive_chunking = (val == "true" || val == "1");
    else if (key == "scheduler_policy")      c.scheduler_policy = val;
    else if (key == "max_active_requests")   c.max_active_requests = std::stoi(val);
    else if (key == "worker_pool_size")      c.worker_pool_size = std::stoi(val);
    else if (key == "request_ttl_ms")        c.request_ttl_ms = std::stoll(val);
    else if (key == "peer_timeout_ms")       c.peer_timeout_ms = std::stoll(val);
    else if (key == "client_poll_timeout_ms") c.client_poll_timeout_ms = std::stoll(val);
    else if (key == "abandon_timeout_ms")    c.abandon_timeout_ms = std::stoll(val);
    else if (key == "peer_completion_timeout_ms") c.peer_completion_timeout_ms = std::stoll(val);
    else if (key == "peer_query_test_delay_ms") c.peer_query_test_delay_ms = std::stoll(val);
    else if (key == "metrics_output_path")   c.metrics_output_path = val;
    else {
      // Unknown key: tolerate to keep configs forward-compatible during dev.
    }
  }

  if (c.node_id.empty())  throw std::runtime_error("config missing node_id");
  if (c.host.empty())     throw std::runtime_error("config missing host");
  if (c.port == 0)        throw std::runtime_error("config missing port");

  return c;
}

}  // namespace mini2
