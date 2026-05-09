cmake_minimum_required(VERSION 3.20)
project(mini2 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Helpful warnings
add_compile_options(-Wall -Wextra -Wpedantic)

# ── Find Protobuf + gRPC (Homebrew on macOS, /opt/homebrew) ─────────────────
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

set(_PROTOC $<TARGET_FILE:protobuf::protoc>)
set(_GRPC_CPP_PLUGIN $<TARGET_FILE:gRPC::grpc_cpp_plugin>)

# ── Generate proto stubs into cpp/generated/ ────────────────────────────────
set(PROTO_SRC ${CMAKE_SOURCE_DIR}/proto/mini2.proto)
set(GEN_DIR   ${CMAKE_SOURCE_DIR}/cpp/generated)
file(MAKE_DIRECTORY ${GEN_DIR})

set(PROTO_PB_H   ${GEN_DIR}/mini2.pb.h)
set(PROTO_PB_CC  ${GEN_DIR}/mini2.pb.cc)
set(GRPC_PB_H    ${GEN_DIR}/mini2.grpc.pb.h)
set(GRPC_PB_CC   ${GEN_DIR}/mini2.grpc.pb.cc)

add_custom_command(
  OUTPUT ${PROTO_PB_H} ${PROTO_PB_CC} ${GRPC_PB_H} ${GRPC_PB_CC}
  COMMAND ${_PROTOC}
    --proto_path=${CMAKE_SOURCE_DIR}/proto
    --cpp_out=${GEN_DIR}
    --grpc_out=${GEN_DIR}
    --plugin=protoc-gen-grpc=${_GRPC_CPP_PLUGIN}
    ${PROTO_SRC}
  DEPENDS ${PROTO_SRC}
  COMMENT "Generating C++ protobuf + gRPC stubs"
  VERBATIM
)

add_library(mini2_proto STATIC
  ${PROTO_PB_CC}
  ${GRPC_PB_CC}
)
target_include_directories(mini2_proto PUBLIC ${GEN_DIR})
target_link_libraries(mini2_proto PUBLIC
  protobuf::libprotobuf
  gRPC::grpc++
  gRPC::grpc++_reflection
)

# ── Core library ────────────────────────────────────────────────────────────
add_library(mini2_core STATIC
  cpp/src/config/ConfigManager.cpp
  cpp/src/grpc/Server.cpp
  cpp/src/grpc/ServiceImpl.cpp
  cpp/src/grpc/ClientPool.cpp
  cpp/src/model/ServiceRequest.cpp
  cpp/src/model/CSVParser.cpp
  cpp/src/query/DataStore.cpp
  cpp/src/query/IndexSet.cpp
  cpp/src/query/LocalQueryEngine.cpp
  cpp/src/scheduler/WorkerPool.cpp
  cpp/src/scheduler/Scheduler.cpp
)
target_include_directories(mini2_core PUBLIC cpp/include)
target_link_libraries(mini2_core PUBLIC mini2_proto)

# ── Server binary ───────────────────────────────────────────────────────────
add_executable(mini2_server cpp/src/main_server.cpp)
target_link_libraries(mini2_server PRIVATE mini2_core)
