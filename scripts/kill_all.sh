#!/usr/bin/env bash
# Stop all mini2 processes (graceful TERM, then KILL fallback).
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# 1) Kill via PID files.
for n in A B C D E F G H I; do
  pid_file="experiments/logs/${n}.pid"
  [ -f "$pid_file" ] || continue
  pid=$(cat "$pid_file")
  if kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
  fi
done

sleep 1

# 2) SIGKILL anything still alive bound to our ports or matching our binaries.
pkill -9 -f mini2_server 2>/dev/null || true
pkill -9 -f "python.*python/server\.py" 2>/dev/null || true

sleep 0.3
remaining=$(ps aux | grep -E "mini2_server|python/server\.py" | grep -v grep | wc -l | tr -d ' ')
echo "[kill_all] cleared. remaining mini2 processes: $remaining"
