#!/bin/bash
# scripts/launch_all.sh

echo "Starting C++ nodes A through H..."
for node in A B C D E F G H; do
  ./build/mini2_server config/tree/${node}.conf > experiments/logs/${node}.log 2>&1 &
  echo $! > experiments/logs/${node}.pid
done

echo "Starting Python node I..."
python3 python/server.py config/tree/I.conf > experiments/logs/I.log 2>&1 &
echo $! > experiments/logs/I.pid

echo "All 9 nodes started in background."
echo "Wait a few seconds for datasets to load..."
