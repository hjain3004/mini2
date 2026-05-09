#include "grpc/Server.hpp"

#include <iostream>

namespace mini2 {

GrpcServer::GrpcServer(const Config& cfg, Mini2ServiceImpl& service)
    : cfg_(cfg), service_(service) {}

void GrpcServer::start() {
  std::string addr = cfg_.host + ":" + std::to_string(cfg_.port);

  grpc::ServerBuilder builder;
  builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
  builder.RegisterService(&service_);

  // Tune max message sizes for large chunks
  builder.SetMaxReceiveMessageSize(16 * 1024 * 1024);  // 16 MB
  builder.SetMaxSendMessageSize(16 * 1024 * 1024);

  server_ = builder.BuildAndStart();
  if (!server_) {
    throw std::runtime_error("Failed to start gRPC server on " + addr);
  }

  running_.store(true);
  std::cout << "[grpc] server " << cfg_.node_id
            << " listening on " << addr << "\n";
}

void GrpcServer::wait() {
  if (server_) {
    server_->Wait();
  }
}

void GrpcServer::shutdown() {
  if (server_ && running_.load()) {
    std::cout << "[grpc] shutting down server " << cfg_.node_id << "\n";
    server_->Shutdown();
    running_.store(false);
  }
}

}  // namespace mini2
