// main_client.cpp — External C++ client for Mini 2.
//
// Usage: mini2_client <target> <field> <op> <value> [max_chunk_records]
// Example: mini2_client 127.0.0.1:50051 borough eq MANHATTAN 1000

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>

#include "mini2.grpc.pb.h"

using grpc::Channel;
using grpc::ChannelArguments;
using grpc::ClientContext;
using grpc::Status;
using mini2::ChunkFetchRequest;
using mini2::ChunkResponse;
using mini2::Mini2Service;
using mini2::QueryAccepted;
using mini2::QueryFilter;
using mini2::QueryRequest;

int main(int argc, char** argv) {
  if (argc < 5) {
    std::cerr << "usage: " << argv[0]
              << " <target> <field> <op> <value> [max_chunk_records]\n";
    return 1;
  }

  const std::string target = argv[1];
  const std::string field = argv[2];
  const std::string op = argv[3];
  const std::string value = argv[4];
  const int32_t max_records = (argc > 5) ? std::atoi(argv[5]) : 500;

  std::cout << "[client] connecting to " << target << "\n";

  ChannelArguments ch_args;
  ch_args.SetMaxReceiveMessageSize(16 * 1024 * 1024);
  ch_args.SetMaxSendMessageSize(16 * 1024 * 1024);

  auto channel = grpc::CreateCustomChannel(
      target, grpc::InsecureChannelCredentials(), ch_args);
  auto stub = Mini2Service::NewStub(channel);

  const std::string client_id =
      "cpp-client-" + std::to_string(
          std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count());

  QueryRequest req;
  req.set_client_id(client_id);
  req.set_query_text("Find " + field + " " + op + " " + value);
  QueryFilter* f = req.mutable_filter();
  f->set_field_name(field);
  f->set_op(op);
  f->set_value(value);
  req.set_requested_chunk_records(max_records);
  req.set_max_chunk_bytes(65536);

  std::cout << "[client] SubmitQuery: " << req.query_text() << "\n";

  QueryAccepted accepted;
  {
    ClientContext ctx;
    Status status = stub->SubmitQuery(&ctx, req, &accepted);
    if (!status.ok()) {
      std::cerr << "[client] SubmitQuery failed: " << status.error_message()
                << "\n";
      return 1;
    }
  }
  std::cout << "[client] QueryAccepted: req_id=" << accepted.request_id()
            << " (" << accepted.message() << ")\n";

  const std::string req_id = accepted.request_id();
  int64_t total_records = 0;
  int64_t chunks = 0;

  std::cout << "[client] polling FetchChunk for req_id=" << req_id << "\n";

  const auto t0 = std::chrono::steady_clock::now();

  while (true) {
    ChunkFetchRequest fetch_req;
    fetch_req.set_client_id(client_id);
    fetch_req.set_request_id(req_id);
    fetch_req.set_max_records(max_records);

    ChunkResponse chunk;
    ClientContext ctx;
    Status status = stub->FetchChunk(&ctx, fetch_req, &chunk);
    if (!status.ok()) {
      std::cerr << "[client] FetchChunk failed: " << status.error_message()
                << "\n";
      break;
    }

    const int count = chunk.records_size();
    total_records += count;
    ++chunks;

    std::cout << "  -> chunk " << chunk.chunk_id() << ": " << count
              << " records, done=" << (chunk.done() ? "true" : "false")
              << ", cancelled=" << (chunk.cancelled() ? "true" : "false")
              << "\n";

    if (chunk.done() || chunk.cancelled()) break;
  }

  const auto t1 = std::chrono::steady_clock::now();
  const double elapsed_s =
      std::chrono::duration<double>(t1 - t0).count();

  std::cout << "[client] finished. fetched " << total_records
            << " records across " << chunks << " chunks in " << elapsed_s
            << "s.\n";
  return 0;
}
