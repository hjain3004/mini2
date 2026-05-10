#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────────────
# check_deps.sh — Check and install all dependencies for mini2 on macOS.
#
# Usage:  bash scripts/check_deps.sh
#
# What it does:
#   1. Checks for Homebrew, cmake, protoc, grpc, python3
#   2. Installs anything missing via Homebrew
#   3. Checks Python packages (grpcio, grpcio-tools, protobuf, matplotlib)
#   4. Installs missing Python packages via pip3
#   5. Verifies the build works
#   6. Verifies Python stubs are generated
#   7. Checks dataset partitions exist
# ──────────────────────────────────────────────────────────────────────────────
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

pass_count=0
fail_count=0
installed=0

ok()         { echo -e "  ${GREEN}✔${NC} $1"; ((pass_count++)) || true; }
warn()       { echo -e "  ${YELLOW}⚠${NC} $1"; }
check_fail() { echo -e "  ${RED}✘${NC} $1"; ((fail_count++)) || true; }
info()       { echo -e "  ${CYAN}→${NC} $1"; }

echo ""
echo "╔═══════════════════════════════════════════════════════════╗"
echo "║       Mini 2 — macOS Dependency Checker & Installer      ║"
echo "╚═══════════════════════════════════════════════════════════╝"
echo ""

# ─── 1. Homebrew ─────────────────────────────────────────────────────────────
echo "── 1. Homebrew ──"

if command -v brew &>/dev/null; then
  ok "Homebrew installed ($(brew --version | head -1))"
else
  check_fail "Homebrew not found"
  info "Installing Homebrew..."
  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
  # Add to PATH for Apple Silicon
  if [ -f /opt/homebrew/bin/brew ]; then
    eval "$(/opt/homebrew/bin/brew shellenv)"
  fi
  ok "Homebrew installed"
  ((installed++)) || true
fi
echo ""

# ─── 2. CMake ────────────────────────────────────────────────────────────────
echo "── 2. CMake ──"

if command -v cmake &>/dev/null; then
  ok "cmake installed ($(cmake --version | head -1 | awk '{print $3}'))"
else
  check_fail "cmake not found"
  info "Installing cmake via Homebrew..."
  brew install cmake
  ok "cmake installed"
  ((installed++)) || true
fi
echo ""

# ─── 3. Protobuf (protoc) ───────────────────────────────────────────────────
echo "── 3. Protobuf ──"

if command -v protoc &>/dev/null; then
  ok "protoc installed ($(protoc --version))"
else
  check_fail "protoc not found"
  info "Installing protobuf via Homebrew..."
  brew install protobuf
  ok "protobuf installed"
  ((installed++)) || true
fi
echo ""

# ─── 4. gRPC ─────────────────────────────────────────────────────────────────
echo "── 4. gRPC (C++) ──"

# Check if grpc++ is findable by pkg-config or if the grpc_cpp_plugin exists
GRPC_OK=false
if pkg-config --exists grpc++ 2>/dev/null; then
  GRPC_VERSION=$(pkg-config --modversion grpc++ 2>/dev/null || echo "unknown")
  ok "gRPC C++ found via pkg-config (${GRPC_VERSION})"
  GRPC_OK=true
elif [ -f /opt/homebrew/bin/grpc_cpp_plugin ] || [ -f /usr/local/bin/grpc_cpp_plugin ]; then
  ok "gRPC C++ found (grpc_cpp_plugin binary exists)"
  GRPC_OK=true
fi

if [ "$GRPC_OK" = false ]; then
  check_fail "gRPC C++ not found"
  info "Installing grpc via Homebrew (this may take a few minutes)..."
  brew install grpc
  ok "gRPC installed"
  ((installed++)) || true
fi
echo ""

# ─── 5. Python 3 ─────────────────────────────────────────────────────────────
echo "── 5. Python 3 ──"

if command -v python3 &>/dev/null; then
  ok "python3 installed ($(python3 --version 2>&1))"
else
  check_fail "python3 not found"
  info "Installing python3 via Homebrew..."
  brew install python3
  ok "python3 installed"
  ((installed++)) || true
fi
echo ""

# ─── 6. Python packages ─────────────────────────────────────────────────────
echo "── 6. Python packages ──"

PY_MISSING=()

check_py_pkg() {
  local pkg="$1"
  local import_name="$2"
  if python3 -c "import ${import_name}" 2>/dev/null; then
    ok "${pkg}"
  else
    check_fail "${pkg} not installed"
    PY_MISSING+=("$pkg")
  fi
}

check_py_pkg "grpcio"       "grpc"
check_py_pkg "grpcio-tools" "grpc_tools"
check_py_pkg "protobuf"     "google.protobuf"
check_py_pkg "matplotlib"   "matplotlib"

if [ ${#PY_MISSING[@]} -gt 0 ]; then
  info "Installing missing Python packages: ${PY_MISSING[*]}"
  # Modern Homebrew Python (PEP 668) requires --break-system-packages or --user
  if pip3 install --user "${PY_MISSING[@]}" 2>/dev/null; then
    ok "Python packages installed (--user)"
  elif pip3 install --break-system-packages "${PY_MISSING[@]}" 2>/dev/null; then
    ok "Python packages installed (--break-system-packages)"
  else
    check_fail "pip3 install failed. Try manually: pip3 install --break-system-packages ${PY_MISSING[*]}"
  fi
  ((installed++)) || true
fi
echo ""

# ─── 7. Project build ───────────────────────────────────────────────────────
echo "── 7. C++ build ──"

if [ -f "$ROOT/build/mini2_server" ]; then
  ok "mini2_server binary exists"
else
  warn "mini2_server not built yet"
  info "Building now..."
  if bash scripts/build.sh > /tmp/mini2_build.log 2>&1; then
    ok "Build successful"
    ((installed++)) || true
  else
    check_fail "Build FAILED — check /tmp/mini2_build.log"
    echo ""
    echo "  Last 10 lines of build log:"
    tail -10 /tmp/mini2_build.log | sed 's/^/    /'
  fi
fi
echo ""

# ─── 8. Python gRPC stubs ───────────────────────────────────────────────────
echo "── 8. Python gRPC stubs ──"

if [ -f "$ROOT/python/generated/mini2_pb2.py" ] && [ -f "$ROOT/python/generated/mini2_pb2_grpc.py" ]; then
  ok "Python stubs exist"
else
  warn "Python stubs not generated"
  info "Generating now..."
  if bash scripts/gen_python_stubs.sh > /dev/null 2>&1; then
    ok "Stubs generated"
    ((installed++)) || true
  else
    check_fail "Stub generation FAILED"
  fi
fi
echo ""

# ─── 9. Dataset partitions ──────────────────────────────────────────────────
echo "── 9. Dataset partitions ──"

PARTITION_COUNT=0
for node in A B C D E F G H I; do
  if [ -f "$ROOT/data/partitions/${node}.csv" ]; then
    ((PARTITION_COUNT++))
  fi
done

if [ "$PARTITION_COUNT" -eq 9 ]; then
  ok "All 9 partitions exist (data/partitions/{A..I}.csv)"
else
  warn "Only ${PARTITION_COUNT}/9 partitions found"
  if [ -f "$ROOT/dataset/311_2020.csv" ]; then
    info "Source dataset found. Run 'python3 scripts/partition_data.py' to generate partitions."
    info "(This takes ~5-10 minutes and is not run automatically)"
  else
    warn "Source dataset not found at dataset/311_2020.csv"
    info "You need the NYC 311 dataset to generate partitions."
  fi
fi
echo ""

# ─── 10. Log directory ──────────────────────────────────────────────────────
echo "── 10. Directories ──"

mkdir -p experiments/logs experiments/metrics_output report/figures
ok "experiments/logs/ exists"
ok "experiments/metrics_output/ exists"
ok "report/figures/ exists"
echo ""

# ─── Summary ─────────────────────────────────────────────────────────────────
echo "╔═══════════════════════════════════════════════════════════╗"
if [ "$fail_count" -eq 0 ]; then
  echo -e "║  ${GREEN}All checks passed!${NC}                                      ║"
else
  echo -e "║  ${YELLOW}Some items need attention (see above).${NC}                   ║"
fi
echo "╚═══════════════════════════════════════════════════════════╝"
echo ""
echo -e "  ${GREEN}✔ Passed:${NC}    $pass_count"
if [ "$installed" -gt 0 ]; then
  echo -e "  ${CYAN}→ Installed:${NC} $installed"
fi
if [ "$fail_count" -gt 0 ]; then
  echo -e "  ${RED}✘ Failed:${NC}    $fail_count"
fi
echo ""

if [ "$PARTITION_COUNT" -lt 9 ] && [ -f "$ROOT/dataset/311_2020.csv" ]; then
  echo "  Next step: python3 scripts/partition_data.py"
elif [ "$fail_count" -eq 0 ] && [ "$PARTITION_COUNT" -eq 9 ]; then
  echo "  Ready to demo! Run: bash scripts/launch_all.sh"
fi
echo ""
