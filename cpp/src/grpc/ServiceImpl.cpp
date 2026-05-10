#include "grpc/ServiceImpl.hpp"

#include <chrono>
#include <iostream>
#include <thread>

namespace mini2 {

std::string Mini2ServiceImpl::generate_request_id() {
  uint64_t id = next_request_id_.fetch_add(1);
  return cfg_.node_id + "-" + std::to_string(id);
}

// ─── Heartbeat ──────────────────────────────────────────────────────────────

grpc::Status Mini2ServiceImpl::Heartbeat(grpc::ServerContext* /*context*/,
                                         const HeartbeatRequest* request,
                                         HeartbeatReply* reply) {
  auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count();

  std::cout << "[heartbeat] " << cfg_.node_id
            << " received heartbeat from " << request->node_id()
            << " (ts=" << request->timestamp_ms() << ")\n";

  reply->set_node_id(cfg_.node_id);
  reply->set_ok(true);
  reply->set_timestamp_ms(now);
  return grpc::Status::OK;
}

// ─── Helpers ────────────────────────────────────────────────────────────────

// Try to emit source_done to our parent for `req_id`, once and only once.
// Caller must NOT hold mutex_. Returns true if a source_done was sent.
static bool emit_done_to_parent_locked_check(
    std::mutex& mutex,
    std::unordered_map<std::string, ActiveQuery>& active_queries,
    const std::string& req_id,
    std::string& out_parent_node) {
  std::lock_guard<std::mutex> lock(mutex);
  auto it = active_queries.find(req_id);
  if (it == active_queries.end()) return false;
  ActiveQuery& aq = it->second;
  if (aq.parent_done_sent) return false;
  if (aq.parent_node.empty()) return false;       // gateway, no parent
  if (!aq.local_done) return false;
  if (!aq.all_children_done()) return false;
  aq.parent_done_sent = true;
  out_parent_node = aq.parent_node;
  return true;
}

void Mini2ServiceImpl::try_emit_done_to_parent(const std::string& req_id) {
  std::string parent_node;
  if (!emit_done_to_parent_locked_check(mutex_, active_queries_, req_id, parent_node)) {
    return;
  }
  auto stub = client_pool_.get_stub(parent_node);
  if (!stub) return;

  PeerChunk done_chunk;
  done_chunk.set_request_id(req_id);
  done_chunk.set_source_node(cfg_.node_id);   // attribute to ourselves
  done_chunk.set_destination_node(parent_node);
  done_chunk.set_chunk_id(0);
  done_chunk.set_source_done(true);

  PeerChunkAck ack;
  grpc::ClientContext ctx;
  ctx.set_deadline(std::chrono::system_clock::now() +
                   std::chrono::milliseconds(cfg_.peer_timeout_ms));
  stub->PushPeerChunk(&ctx, done_chunk, &ack);
  std::cout << "[query] " << cfg_.node_id
            << " emitted source_done -> " << parent_node
            << " for " << req_id << "\n";
}

// Mark a child as completed for `req_id`. Caller must NOT hold mutex_.
// Returns true if newly added (was not already in completed_children).
bool Mini2ServiceImpl::mark_child_completed(const std::string& req_id,
                                            const std::string& child_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = active_queries_.find(req_id);
  if (it == active_queries_.end()) return false;
  ActiveQuery& aq = it->second;
  for (const auto& c : aq.completed_children) {
    if (c == child_id) return false;
  }
  aq.completed_children.push_back(child_id);
  return true;
}

// ─── External client RPCs ───────────────────────────────────────────────────

grpc::Status Mini2ServiceImpl::SubmitQuery(grpc::ServerContext* /*context*/,
                                           const QueryRequest* request,
                                           QueryAccepted* reply) {
  std::string req_id = generate_request_id();
  std::cout << "[query] " << cfg_.node_id << " received query " << req_id
            << " from " << request->client_id() << "\n";

  // Run the local query synchronously (gateway only — workers do this in PeerQuery thread).
  bool force_linear = request->force_linear_scan();
  std::vector<ServiceRecord> results = query_engine_.run(request->filter(), force_linear);

  time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

  ActiveQuery aq;
  aq.request_id = req_id;
  aq.client_id = request->client_id();
  aq.results = std::move(results);
  aq.last_activity = now;
  aq.created_at = now;
  aq.local_done = true;
  aq.parent_node = "";  // gateway has no parent
  for (const auto& neighbor : cfg_.neighbors) {
    aq.expected_children.push_back(neighbor.id);
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    active_queries_[req_id] = std::move(aq);
  }

  QueryFilter filter = request->filter();

  // Forward PeerQuery asynchronously to every neighbor.
  for (const auto& neighbor : cfg_.neighbors) {
    worker_pool_.submit([this, neighbor, req_id, filter, force_linear]() {
      auto stub = client_pool_.get_stub(neighbor.id);
      if (!stub) {
        mark_child_completed(req_id, neighbor.id);
        try_emit_done_to_parent(req_id);
        return;
      }

      PeerQueryRequest pq_req;
      pq_req.set_request_id(req_id);
      pq_req.set_origin_node(cfg_.node_id);
      pq_req.set_parent_node(cfg_.node_id);
      pq_req.set_sender_node(cfg_.node_id);
      *pq_req.mutable_filter() = filter;
      pq_req.set_ttl_ms(60000);
      pq_req.add_visited_nodes(cfg_.node_id);
      pq_req.set_chunk_records(500);
      pq_req.set_max_chunk_bytes(65536);
      pq_req.set_force_linear_scan(force_linear);

      PeerQueryAck pq_reply;
      grpc::ClientContext context;
      context.set_deadline(std::chrono::system_clock::now() +
                           std::chrono::milliseconds(cfg_.peer_timeout_ms));
      grpc::Status status = stub->PeerQuery(&context, pq_req, &pq_reply);

      // Treat both (a) RPC failure and (b) accepted=false (e.g. duplicate, UNIMPLEMENTED)
      // as "this neighbor is not going to deliver as our child" — mark complete now.
      if (!status.ok() || !pq_reply.accepted()) {
        mark_child_completed(req_id, neighbor.id);
        try_emit_done_to_parent(req_id);
      }
    });
  }

  reply->set_request_id(req_id);
  reply->set_accepted(true);
  reply->set_message("accepted");
  return grpc::Status::OK;
}

grpc::Status Mini2ServiceImpl::FetchChunk(grpc::ServerContext* /*context*/,
                                          const ChunkFetchRequest* request,
                                          ChunkResponse* reply) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = active_queries_.find(request->request_id());
  if (it == active_queries_.end()) {
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "request_id not found");
  }

  ActiveQuery& aq = it->second;
  aq.last_activity = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

  reply->set_request_id(aq.request_id);
  reply->set_chunk_id(aq.next_chunk_id++);

  if (aq.cancelled) {
    reply->set_done(true);
    reply->set_cancelled(true);
    reply->set_message("cancelled");
    return grpc::Status::OK;
  }

  size_t available = aq.results.size() - aq.fetched_count;
  int records_to_send = 0;
  if (available > 0) {
    records_to_send = std::min(static_cast<size_t>(request->max_records()), available);
    for (int i = 0; i < records_to_send; ++i) {
      *reply->add_records() = aq.results[aq.fetched_count + i].to_proto();
    }
    aq.fetched_count += records_to_send;
    available -= records_to_send;
  }

  reply->set_done(available == 0 && aq.local_done && aq.all_children_done());
  reply->set_cancelled(false);

  std::cout << "[query] " << cfg_.node_id << " sending chunk "
            << reply->chunk_id() << " for " << aq.request_id
            << " (" << records_to_send << " records, done=" << reply->done()
            << ", expected=" << aq.expected_children.size()
            << ", completed=" << aq.completed_children.size()
            << ")\n";

  return grpc::Status::OK;
}

grpc::Status Mini2ServiceImpl::CancelQuery(grpc::ServerContext* /*context*/,
                                           const CancelRequest* request,
                                           CancelAck* reply) {
  reply->set_request_id(request->request_id());

  bool found = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_queries_.find(request->request_id());
    if (it != active_queries_.end() && !it->second.cancelled) {
      it->second.cancelled = true;
      found = true;
      std::cout << "[query] " << cfg_.node_id << " cancelled query "
                << request->request_id() << "\n";
    }
  }

  reply->set_cancelled(found);

  if (found) {
    // Cancel any pending scheduler job.
    scheduler_.cancel(request->request_id());
    // Propagate PeerCancel to children (without mutex).
    propagate_cancel_to_children(request->request_id());
  }

  return grpc::Status::OK;
}

// ─── Internal peer RPCs ─────────────────────────────────────────────────────

grpc::Status Mini2ServiceImpl::PeerQuery(grpc::ServerContext* /*context*/,
                                         const PeerQueryRequest* request,
                                         PeerQueryAck* reply) {
  reply->set_request_id(request->request_id());
  reply->set_receiver_node(cfg_.node_id);

  // Visited check: if I am already in the path, refuse.
  for (const auto& vn : request->visited_nodes()) {
    if (vn == cfg_.node_id) {
      reply->set_accepted(false);
      reply->set_message("already visited");
      return grpc::Status::OK;
    }
  }

  // Dedup: if this request_id already has state on this node, another path
  // got here first. Refuse so the caller marks us complete in their bookkeeping.
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_queries_.find(request->request_id()) != active_queries_.end()) {
      reply->set_accepted(false);
      reply->set_message("duplicate path");
      return grpc::Status::OK;
    }
  }

  std::string req_id = request->request_id();
  std::string origin_node = request->origin_node();
  std::string parent_node = request->sender_node();
  QueryFilter filter = request->filter();
  int32_t ttl_ms = request->ttl_ms();
  int32_t chunk_records_req = request->chunk_records();
  int32_t max_chunk_bytes_req = request->max_chunk_bytes();
  bool force_linear = request->force_linear_scan();

  std::vector<std::string> next_visited;
  for (const auto& vn : request->visited_nodes()) next_visited.push_back(vn);
  next_visited.push_back(cfg_.node_id);

  // Decide which neighbors we'll forward to.
  std::vector<std::string> expected;
  for (const auto& n : cfg_.neighbors) {
    if (n.id == parent_node) continue;
    bool visited = false;
    for (const auto& vn : next_visited) {
      if (n.id == vn) { visited = true; break; }
    }
    if (visited) continue;
    expected.push_back(n.id);
  }

  // Register state first (atomic with the dedup check above? we re-acquire here;
  // a race could let two simultaneous arrivals both pass the check. Acceptable for
  // step 6: one inserts, the other overwrites with identical metadata. The
  // double local-query is the worst case and self-corrects via dedup once the
  // second arrival's PeerQuery to neighbors comes back.).
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_queries_.find(req_id) != active_queries_.end()) {
      reply->set_accepted(false);
      reply->set_message("duplicate path (race)");
      return grpc::Status::OK;
    }
    ActiveQuery aq;
    aq.request_id = req_id;
    aq.parent_node = parent_node;
    aq.expected_children = expected;
    aq.last_activity = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    aq.created_at = aq.last_activity;
    active_queries_[req_id] = std::move(aq);
  }

  reply->set_accepted(true);

  // Forward PeerQuery to each unvisited neighbor (worker pool).
  for (const auto& neighbor_id : expected) {
    worker_pool_.submit([this, neighbor_id, req_id, origin_node, filter, ttl_ms,
                         chunk_records_req, max_chunk_bytes_req, next_visited, force_linear]() {
      auto stub = client_pool_.get_stub(neighbor_id);
      if (!stub) {
        mark_child_completed(req_id, neighbor_id);
        try_emit_done_to_parent(req_id);
        return;
      }
      PeerQueryRequest pq_req;
      pq_req.set_request_id(req_id);
      pq_req.set_origin_node(origin_node);
      pq_req.set_parent_node(cfg_.node_id);
      pq_req.set_sender_node(cfg_.node_id);
      *pq_req.mutable_filter() = filter;
      pq_req.set_ttl_ms(ttl_ms);
      for (const auto& vn : next_visited) pq_req.add_visited_nodes(vn);
      pq_req.set_chunk_records(chunk_records_req);
      pq_req.set_max_chunk_bytes(max_chunk_bytes_req);
      pq_req.set_force_linear_scan(force_linear);

      PeerQueryAck pq_reply;
      grpc::ClientContext ctx;
      ctx.set_deadline(std::chrono::system_clock::now() +
                       std::chrono::milliseconds(cfg_.peer_timeout_ms));
      grpc::Status status = stub->PeerQuery(&ctx, pq_req, &pq_reply);
      if (!status.ok() || !pq_reply.accepted()) {
        mark_child_completed(req_id, neighbor_id);
        try_emit_done_to_parent(req_id);
      }
    });
  }

  // Run local query (worker pool); on completion, hand the result set to the
  // scheduler which pushes chunks to the parent under the configured policy.
  int chunk_records = chunk_records_req > 0 ? chunk_records_req : cfg_.default_chunk_records;
  worker_pool_.submit([this, req_id, filter, parent_node, chunk_records, force_linear]() {
    std::vector<ServiceRecord> results = query_engine_.run(filter, force_linear);
    scheduler_.submit(req_id, parent_node, std::move(results),
                      static_cast<std::size_t>(chunk_records));
  });

  return grpc::Status::OK;
}

void Mini2ServiceImpl::on_scheduler_local_done(const std::string& req_id) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_queries_.find(req_id);
    if (it != active_queries_.end()) {
      it->second.local_done = true;
    }
  }
  try_emit_done_to_parent(req_id);
}

grpc::Status Mini2ServiceImpl::PushPeerChunk(grpc::ServerContext* /*context*/,
                                             const PeerChunk* request,
                                             PeerChunkAck* reply) {
  reply->set_request_id(request->request_id());
  reply->set_receiver_node(cfg_.node_id);

  // Snapshot of fields we'll need outside the mutex.
  enum class Mode { Gateway, Intermediate, Unknown };
  Mode mode = Mode::Unknown;
  std::string parent_node_snapshot;
  bool cancelled = false;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_queries_.find(request->request_id());
    if (it == active_queries_.end()) {
      reply->set_accepted(false);
      return grpc::Status::OK;
    }
    ActiveQuery& aq = it->second;
    if (aq.cancelled) {
      cancelled = true;
    } else {
      aq.last_activity = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
      if (aq.parent_node.empty()) {
        mode = Mode::Gateway;
        // Append records into our queue (we're A, the gateway).
        for (const auto& rec : request->records()) {
          aq.results.push_back(ServiceRecord::from_proto(rec));
        }
      } else {
        mode = Mode::Intermediate;
        parent_node_snapshot = aq.parent_node;
      }
    }
  }

  if (cancelled) {
    reply->set_accepted(false);
    return grpc::Status::OK;
  }

  // If intermediate AND this chunk has records, forward upward (without the mutex).
  // Rewrite source_node to ourselves so the parent's child-bookkeeping is consistent.
  if (mode == Mode::Intermediate && request->records_size() > 0) {
    auto stub = client_pool_.get_stub(parent_node_snapshot);
    if (stub) {
      PeerChunk fwd;
      fwd.set_request_id(request->request_id());
      fwd.set_source_node(cfg_.node_id);
      fwd.set_destination_node(parent_node_snapshot);
      fwd.set_chunk_id(request->chunk_id());
      for (const auto& rec : request->records()) {
        *fwd.add_records() = rec;
      }
      fwd.set_source_done(false);
      PeerChunkAck ack;
      grpc::ClientContext ctx;
      ctx.set_deadline(std::chrono::system_clock::now() +
                       std::chrono::milliseconds(cfg_.peer_timeout_ms));
      stub->PushPeerChunk(&ctx, fwd, &ack);
    }
  }

  // Handle source_done: never forward as-is. Update bookkeeping; aggregation
  // helper emits our own source_done if appropriate.
  if (request->source_done()) {
    if (mark_child_completed(request->request_id(), request->source_node())) {
      std::cout << "[query] " << cfg_.node_id
                << " marked child " << request->source_node() << " done for "
                << request->request_id() << "\n";
    }
    try_emit_done_to_parent(request->request_id());
  }

  reply->set_accepted(true);
  return grpc::Status::OK;
}

grpc::Status Mini2ServiceImpl::PeerCancel(grpc::ServerContext* /*context*/,
                                          const PeerCancelRequest* request,
                                          PeerCancelAck* reply) {
  reply->set_request_id(request->request_id());

  bool found = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_queries_.find(request->request_id());
    if (it != active_queries_.end() && !it->second.cancelled) {
      it->second.cancelled = true;
      found = true;
      std::cout << "[cancel] " << cfg_.node_id << " peer-cancelled "
                << request->request_id()
                << " (from " << request->sender_node() << ")\n";
    }
  }

  reply->set_cancelled(found);

  if (found) {
    scheduler_.cancel(request->request_id());
    propagate_cancel_to_children(request->request_id());
  }

  return grpc::Status::OK;
}

// ─── Cancellation propagation ───────────────────────────────────────────────

void Mini2ServiceImpl::propagate_cancel_to_children(const std::string& req_id) {
  // Snapshot the children we need to cancel.
  std::vector<std::string> children_to_cancel;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_queries_.find(req_id);
    if (it == active_queries_.end()) return;
    const ActiveQuery& aq = it->second;
    for (const auto& child : aq.expected_children) {
      // Only cancel children that haven't already completed.
      bool completed = false;
      for (const auto& c : aq.completed_children) {
        if (c == child) { completed = true; break; }
      }
      if (!completed) children_to_cancel.push_back(child);
    }
  }

  // Send PeerCancel to each (without the mutex).
  for (const auto& child_id : children_to_cancel) {
    worker_pool_.submit([this, req_id, child_id]() {
      auto stub = client_pool_.get_stub(child_id);
      if (!stub) return;
      PeerCancelRequest cancel_req;
      cancel_req.set_request_id(req_id);
      cancel_req.set_sender_node(cfg_.node_id);
      cancel_req.set_reason("cancelled");

      PeerCancelAck cancel_ack;
      grpc::ClientContext ctx;
      ctx.set_deadline(std::chrono::system_clock::now() +
                       std::chrono::milliseconds(cfg_.peer_timeout_ms));
      auto status = stub->PeerCancel(&ctx, cancel_req, &cancel_ack);
      if (status.ok()) {
        std::cout << "[cancel] " << cfg_.node_id
                  << " propagated PeerCancel -> " << child_id
                  << " for " << req_id << "\n";
      }
    });
  }
}

// ─── TTL Janitor ────────────────────────────────────────────────────────────

void Mini2ServiceImpl::run_janitor(const std::atomic<bool>& shutdown) {
  while (!shutdown.load()) {
    // Sleep 500ms between sweeps (interruptible).
    for (int i = 0; i < 5 && !shutdown.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (shutdown.load()) return;

    time_t now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());

    // Collect request_ids that need reaping.
    std::vector<std::string> to_cancel;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (auto& [req_id, aq] : active_queries_) {
        if (aq.cancelled) continue;

        // TTL expiry: request has been alive too long.
        double age_ms = std::difftime(now, aq.created_at) * 1000.0;
        if (age_ms > static_cast<double>(cfg_.request_ttl_ms)) {
          std::cout << "[janitor] " << cfg_.node_id << " TTL expired "
                    << req_id << " (age=" << age_ms << "ms)\n";
          aq.cancelled = true;
          to_cancel.push_back(req_id);
          continue;
        }

        // Abandon timeout: gateway-only — client hasn't fetched in too long.
        if (aq.parent_node.empty()) {  // gateway
          double idle_ms = std::difftime(now, aq.last_activity) * 1000.0;
          if (idle_ms > static_cast<double>(cfg_.abandon_timeout_ms)) {
            std::cout << "[janitor] " << cfg_.node_id << " abandoned "
                      << req_id << " (idle=" << idle_ms << "ms)\n";
            aq.cancelled = true;
            to_cancel.push_back(req_id);
            continue;
          }
        }
      }
    }

    // Propagate cancellation for each reaped request (without mutex).
    for (const auto& req_id : to_cancel) {
      scheduler_.cancel(req_id);
      propagate_cancel_to_children(req_id);
    }

    // Garbage-collect completed+cancelled requests older than 2x TTL.
    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (auto it = active_queries_.begin(); it != active_queries_.end();) {
        const ActiveQuery& aq = it->second;
        double age_ms = std::difftime(now, aq.created_at) * 1000.0;
        bool fully_done = aq.cancelled ||
                          (aq.local_done && aq.all_children_done());
        if (fully_done && age_ms > static_cast<double>(cfg_.request_ttl_ms) * 2.0) {
          std::cout << "[janitor] " << cfg_.node_id << " reaped "
                    << it->first << "\n";
          it = active_queries_.erase(it);
        } else {
          ++it;
        }
      }
    }
  }
}

}  // namespace mini2
