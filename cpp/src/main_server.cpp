#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>

#include "config/ConfigManager.hpp"
#include "grpc/Server.hpp"
#include "grpc/ServiceImpl.hpp"
#include "grpc/ClientPool.hpp"
#include "query/DataStore.hpp"
#include "query/IndexSet.hpp"
#include "query/LocalQueryEngine.hpp"
#include "scheduler/WorkerPool.hpp"
#include "scheduler/Scheduler.hpp"
#include "mini2.grpc.pb.h"

// ─── Global shutdown flag ───────────────────────────────────────────────────

// Main server implementation for mini2 distributed query system
static std::atomic<bool> g_shutdown{false};

static void signal_handler(int /*sig*/) {
  g_shutdown.store(true);
}

// ─── Heartbeat loop ─────────────────────────────────────────────────────────

static void heartbeat_loop(const mini2::Config& cfg,
                           mini2::GrpcClientPool& pool) {
  std::cout << "[heartbeat] Starting heartbeat loop for node " << cfg.node_id << "\n";
  while (!g_shutdown.load()) {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();

    for (const auto& neighbor_id : pool.neighbor_ids()) {
      try {
        auto* stub = pool.get_stub(neighbor_id);

        mini2::HeartbeatRequest req;
        req.set_node_id(cfg.node_id);
        req.set_timestamp_ms(now);

        mini2::HeartbeatReply reply;
        grpc::ClientContext ctx;
        ctx.set_deadline(std::chrono::system_clock::now() +
                         std::chrono::milliseconds(cfg.peer_timeout_ms));

        auto status = stub->Heartbeat(&ctx, req, &reply);
        if (status.ok()) {
          std::cout << "[heartbeat] " << cfg.node_id
                    << " -> " << neighbor_id
                    << " OK (reply_node=" << reply.node_id()
                    << ", reply_ts=" << reply.timestamp_ms() << ")\n";
        } else {
          std::cerr << "[heartbeat] " << cfg.node_id
                    << " -> " << neighbor_id
                    << " FAILED: " << status.error_message() << "\n";
        }
      } catch (const std::exception& e) {
        std::cerr << "[heartbeat] " << cfg.node_id
                  << " -> " << neighbor_id
                  << " ERROR: " << e.what() << "\n";
      }
    }

    // Sleep 2 seconds between heartbeat rounds
    for (int i = 0; i < 20 && !g_shutdown.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
}

// ─── Main ───────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: mini2_server <config-path>\n";
    return 1;
  }

  // Register signal handlers for graceful shutdown
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  try {
    auto cfg = mini2::ConfigManager::load(argv[1]);

    std::cout << "[mini2_server] Configuration loaded successfully for node " << cfg.node_id << "\n";

    std::cout << "[mini2_server] node_id=" << cfg.node_id
              << " role=" << cfg.role
              << " host=" << cfg.host
              << " port=" << cfg.port
              << " dataset=" << cfg.dataset_path
              << " neighbors=" << cfg.neighbors.size()
              << " overlay=" << cfg.overlay
              << "\n";

    for (const auto& n : cfg.neighbors) {
      std::cout << "  -> " << n.id << " @ " << n.host << ":" << n.port << "\n";
    }

    // Load shard and build indexes
    mini2::DataStore store;
    store.load(cfg.dataset_path);
    
    mini2::IndexSet index;
    index.build(store);

    mini2::LocalQueryEngine query_engine(store, index);

    // Create client pool for outbound RPCs
    mini2::GrpcClientPool client_pool(cfg);

    // Worker pool for one-shot async tasks (PeerQuery forwarding, local
    // query execution). Scheduler drives chunk-push fairness.
    mini2::WorkerPool worker_pool(cfg.worker_pool_size > 0
                                      ? cfg.worker_pool_size : 8);

    auto policy = mini2::Scheduler::policy_from_string(cfg.scheduler_policy);
    std::cout << "[scheduler] policy=" << cfg.scheduler_policy
              << " worker_pool=" << (cfg.worker_pool_size > 0
                                          ? cfg.worker_pool_size : 8) << "\n";

    // Forward declare service so the scheduler callback can reach it.
    std::unique_ptr<mini2::Mini2ServiceImpl> service_ptr;
    mini2::Scheduler scheduler(
        cfg, client_pool, policy,
        [&service_ptr](const std::string& req_id) {
          if (service_ptr) service_ptr->on_scheduler_local_done(req_id);
        });

    service_ptr = std::make_unique<mini2::Mini2ServiceImpl>(
        cfg, query_engine, client_pool, worker_pool, scheduler);

    scheduler.start();

    // Create and start gRPC server
    mini2::GrpcServer server(cfg, *service_ptr);
    server.start();

    // Start heartbeat thread
    std::thread hb_thread(heartbeat_loop, std::cref(cfg),
                          std::ref(client_pool));

    // Step 9: Start TTL janitor thread (reaps expired/abandoned queries).
    std::thread janitor_thread([&service_ptr]() {
      if (service_ptr) service_ptr->run_janitor(g_shutdown);
    });

    std::cout << "[mini2_server] " << cfg.node_id
              << " running. Press Ctrl+C to stop.\n";

    // Wait for shutdown signal
    while (!g_shutdown.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "[mini2_server] " << cfg.node_id << " shutting down...\n";

    // Clean up — order matters: stop accepting RPCs, then drain scheduler
    // and worker pool, then join helpers.
    server.shutdown();
    scheduler.stop();
    worker_pool.stop();
    if (hb_thread.joinable()) {
      hb_thread.join();
    }
    if (janitor_thread.joinable()) {
      janitor_thread.join();
    }

    std::cout << "[mini2_server] " << cfg.node_id << " stopped.\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "[mini2_server] fatal: " << e.what() << "\n";
    return 2;
  }
}
