#include "grpc/ClientPool.hpp"

#include <iostream>
#include <stdexcept>

namespace mini2 {

GrpcClientPool::GrpcClientPool(const Config& cfg) : cfg_(cfg) {
  for (const auto& n : cfg_.neighbors) {
    neighbor_map_[n.id] = n;
  }
}

Mini2Service::Stub* GrpcClientPool::get_stub(const std::string& neighbor_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Return existing stub if already created
  auto it = stubs_.find(neighbor_id);
  if (it != stubs_.end()) {
    return it->second.stub.get();
  }

  // Look up neighbor info
  auto nit = neighbor_map_.find(neighbor_id);
  if (nit == neighbor_map_.end()) {
    throw std::runtime_error("Unknown neighbor: " + neighbor_id);
  }

  // Create channel and stub
  const auto& entry = nit->second;
  std::string target = entry.host + ":" + std::to_string(entry.port);

  grpc::ChannelArguments args;
  args.SetMaxReceiveMessageSize(16 * 1024 * 1024);
  args.SetMaxSendMessageSize(16 * 1024 * 1024);

  auto channel = grpc::CreateCustomChannel(
      target, grpc::InsecureChannelCredentials(), args);

  StubEntry se;
  se.channel = channel;
  se.stub = Mini2Service::NewStub(channel);

  auto* raw = se.stub.get();
  stubs_[neighbor_id] = std::move(se);

  std::cout << "[client_pool] " << cfg_.node_id
            << " created stub for " << neighbor_id
            << " @ " << target << "\n";

  return raw;
}

bool GrpcClientPool::has_neighbor(const std::string& neighbor_id) const {
  return neighbor_map_.count(neighbor_id) > 0;
}

std::vector<std::string> GrpcClientPool::neighbor_ids() const {
  std::vector<std::string> ids;
  ids.reserve(neighbor_map_.size());
  for (const auto& [id, _] : neighbor_map_) {
    ids.push_back(id);
  }
  return ids;
}

}  // namespace mini2
