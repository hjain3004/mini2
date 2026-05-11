#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mini2 {

struct NeighborEntry {
  std::string id;
  std::string host;
  uint16_t port = 0;
};

struct Config {
  // identity
  std::string node_id;
  std::string role;          // "gateway" or "worker"
  std::string language;      // "cpp" or "python"
  std::string host;
  uint16_t    port = 0;

  // data
  std::string dataset_path;

  // topology
  std::string overlay;       // "tree" or "grid"
  std::vector<NeighborEntry> neighbors;

  // chunking
  int32_t  default_chunk_records = 500;
  int64_t  max_chunk_bytes       = 65536;
  bool     adaptive_chunking     = false;

  // scheduler
  std::string scheduler_policy   = "round_robin";   // or "greedy"
  int32_t     max_active_requests = 32;
  int32_t     worker_pool_size    = 8;

  // timeouts (ms)
  int64_t request_ttl_ms        = 10000;
  int64_t peer_timeout_ms       = 3000;
  int64_t client_poll_timeout_ms = 1000;
  int64_t abandon_timeout_ms    = 15000;
  // Step 14 (Fix #5): if a forwarded child hasn't returned source_done within
  // this many ms after the request was registered, the parent force-completes
  // it and serves the result as `partial=true` to the client.
  int64_t peer_completion_timeout_ms = 30000;
  // Test-only: artificially delay this node's source_done emission by this
  // many ms, to exercise the parent's peer_completion_timeout path. 0 = off.
  int64_t peer_query_test_delay_ms = 0;

  // metrics
  std::string metrics_output_path;
};

class ConfigManager {
public:
  // Loads a key=value config from `path`. Throws std::runtime_error on parse error.
  static Config load(const std::string& path);
};

}  // namespace mini2
