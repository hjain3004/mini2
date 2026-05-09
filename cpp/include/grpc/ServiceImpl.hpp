#pragma once

#include "config/ConfigManager.hpp"
#include "query/LocalQueryEngine.hpp"
#include "grpc/ClientPool.hpp"
#include "scheduler/WorkerPool.hpp"
#include "scheduler/Scheduler.hpp"
#include "mini2.grpc.pb.h"

#include <grpcpp/grpcpp.h>
#include <iostream>
#include <string>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <vector>

namespace mini2 {

/**
 * Mini2ServiceImpl — gRPC service implementation.
 *
 * Step 2: Only Heartbeat is implemented. All other RPCs return UNIMPLEMENTED.
 * Subsequent steps fill in SubmitQuery, FetchChunk, PeerQuery, etc.
 */
struct ActiveQuery {
  std::string request_id;
  std::string client_id;
  std::vector<ServiceRecord> results;
  size_t fetched_count = 0;
  bool cancelled = false;
  uint32_t next_chunk_id = 0;
  time_t last_activity = 0;

  // Peer forwarding state
  std::string parent_node;
  std::vector<std::string> expected_children;
  std::vector<std::string> completed_children;
  bool local_done = false;
  bool parent_done_sent = false;   // emit source_done to parent exactly once

  bool all_children_done() const {
    return expected_children.size() == completed_children.size();
  }
};

class Mini2ServiceImpl final : public Mini2Service::Service {
public:
  Mini2ServiceImpl(const Config& cfg,
                   const LocalQueryEngine& query_engine,
                   GrpcClientPool& client_pool,
                   WorkerPool& worker_pool,
                   Scheduler& scheduler)
      : cfg_(cfg), query_engine_(query_engine), client_pool_(client_pool),
        worker_pool_(worker_pool), scheduler_(scheduler) {}

  // Called by Scheduler when a request's local result push is exhausted.
  // Marks local_done and tries to emit source_done upward.
  void on_scheduler_local_done(const std::string& req_id);

  // ─── Health check ─────────────────────────────────────────────────────────

  grpc::Status Heartbeat(grpc::ServerContext* context,
                         const HeartbeatRequest* request,
                         HeartbeatReply* reply) override;

  // ─── External client RPCs (stub — step 4) ────────────────────────────────

  grpc::Status SubmitQuery(grpc::ServerContext* context,
                           const QueryRequest* request,
                           QueryAccepted* reply) override;

  grpc::Status FetchChunk(grpc::ServerContext* context,
                          const ChunkFetchRequest* request,
                          ChunkResponse* reply) override;

  grpc::Status CancelQuery(grpc::ServerContext* context,
                           const CancelRequest* request,
                           CancelAck* reply) override;

  // ─── Internal peer RPCs (stub — steps 5-6) ───────────────────────────────

  grpc::Status PeerQuery(grpc::ServerContext* context,
                         const PeerQueryRequest* request,
                         PeerQueryAck* reply) override;

  grpc::Status PushPeerChunk(grpc::ServerContext* context,
                             const PeerChunk* request,
                             PeerChunkAck* reply) override;

  grpc::Status PeerCancel(grpc::ServerContext* context,
                          const PeerCancelRequest* request,
                          PeerCancelAck* reply) override;

private:
  const Config& cfg_;
  const LocalQueryEngine& query_engine_;
  GrpcClientPool& client_pool_;
  WorkerPool& worker_pool_;
  Scheduler& scheduler_;

  std::mutex mutex_;
  std::unordered_map<std::string, ActiveQuery> active_queries_;
  std::atomic<uint64_t> next_request_id_{1};

  std::string generate_request_id();

  // Multi-hop helpers (must be called WITHOUT mutex_ held).
  bool mark_child_completed(const std::string& req_id,
                            const std::string& child_id);
  void try_emit_done_to_parent(const std::string& req_id);
};

}  // namespace mini2
