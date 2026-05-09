#pragma once

#include "grpc/ServiceImpl.hpp"
#include "config/ConfigManager.hpp"

#include <grpcpp/grpcpp.h>
#include <memory>
#include <string>
#include <thread>
#include <atomic>

namespace mini2 {

/**
 * GrpcServer — wraps grpc::Server lifecycle.
 *
 * Binds to host:port from Config, registers Mini2ServiceImpl,
 * and provides start/wait/shutdown semantics.
 */
class GrpcServer {
public:
  explicit GrpcServer(const Config& cfg, Mini2ServiceImpl& service);

  /// Start the gRPC server (non-blocking).
  void start();

  /// Block until the server shuts down.
  void wait();

  /// Graceful shutdown.
  void shutdown();

  bool is_running() const { return running_.load(); }

private:
  const Config& cfg_;
  Mini2ServiceImpl& service_;
  std::unique_ptr<grpc::Server> server_;
  std::atomic<bool> running_{false};
};

}  // namespace mini2
