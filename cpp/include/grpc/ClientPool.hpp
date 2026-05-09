#pragma once

#include "config/ConfigManager.hpp"
#include "mini2.grpc.pb.h"

#include <grpcpp/grpcpp.h>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace mini2 {

/**
 * GrpcClientPool — maintains one gRPC stub per neighbor.
 *
 * Stubs are lazily created on first access and reused thereafter.
 * gRPC stubs are thread-safe, so no per-call locking is needed after creation.
 */
class GrpcClientPool {
public:
  explicit GrpcClientPool(const Config& cfg);

  /// Get (or create) the stub for a given neighbor id.
  /// Throws std::runtime_error if neighbor_id is not in the config.
  Mini2Service::Stub* get_stub(const std::string& neighbor_id);

  /// Check if a neighbor exists in the config.
  bool has_neighbor(const std::string& neighbor_id) const;

  /// Get all neighbor IDs.
  std::vector<std::string> neighbor_ids() const;

private:
  const Config& cfg_;
  std::mutex mutex_;

  struct StubEntry {
    std::shared_ptr<grpc::Channel> channel;
    std::unique_ptr<Mini2Service::Stub> stub;
  };
  std::unordered_map<std::string, StubEntry> stubs_;

  // neighbor id -> NeighborEntry (from config)
  std::unordered_map<std::string, NeighborEntry> neighbor_map_;
};

}  // namespace mini2
