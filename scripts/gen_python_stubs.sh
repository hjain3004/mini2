#!/usr/bin/env bash
# Generate Python gRPC stubs from mini2.proto
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROTO_DIR="${ROOT}/proto"
OUT_DIR="${ROOT}/python/generated"

mkdir -p "${OUT_DIR}"

python3 -m grpc_tools.protoc \
  --proto_path="${PROTO_DIR}" \
  --python_out="${OUT_DIR}" \
  --grpc_python_out="${OUT_DIR}" \
  "${PROTO_DIR}/mini2.proto"

# Create __init__.py so the directory is importable
touch "${OUT_DIR}/__init__.py"

echo "[gen_python_stubs] generated stubs in ${OUT_DIR}"
