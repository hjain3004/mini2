# Mini 2 — Windows Machine Setup Guide (m2)

> **For the teammate running nodes E, F, G, H, I on a Windows laptop.**
> This guide covers everything needed to set up and run your half of the cluster using **WSL 2**.

---

## Table of Contents

1. [Install WSL 2](#1-install-wsl-2)
2. [Install Dependencies Inside WSL](#2-install-dependencies-inside-wsl)
3. [Get the Project + Dataset](#3-get-the-project--dataset)
4. [Build & Verify](#4-build--verify)
5. [Network Setup (Critical)](#5-network-setup-critical)
6. [Launch Your Nodes](#6-launch-your-nodes)
7. [Verify Everything Works](#7-verify-everything-works)
8. [Shutting Down](#8-shutting-down)
9. [Troubleshooting](#9-troubleshooting)

---

## 1. Install WSL 2

WSL (Windows Subsystem for Linux) lets you run a full Linux environment inside Windows. Our C++ build system requires it.

**Open PowerShell as Administrator** (right-click PowerShell → "Run as administrator"):

```powershell
wsl --install -d Ubuntu-22.04
```

- If prompted, **restart your laptop**.
- After reboot, the **Ubuntu** app will open automatically (or find it in the Start menu).
- It will ask you to create a **username and password** — pick something simple, you'll need it for `sudo`.

**Verify WSL is working:**
```powershell
wsl --version
```

Should show WSL version 2.x.

> [!IMPORTANT]
> All remaining commands in this guide are run **inside the Ubuntu/WSL terminal**, NOT in PowerShell. Open the Ubuntu app from the Start menu.

---

## 2. Install Dependencies Inside WSL

Open the **Ubuntu terminal** and run:

```bash
# Update package lists
sudo apt update

# Install build tools
sudo apt install -y build-essential cmake pkg-config git

# Install gRPC + Protobuf
sudo apt install -y libgrpc++-dev libprotobuf-dev protobuf-compiler protobuf-compiler-grpc

# Install Python 3 + pip
sudo apt install -y python3 python3-pip

# Install Python packages
pip3 install grpcio grpcio-tools protobuf matplotlib
```

**Verify installations:**
```bash
protoc --version          # Should show "libprotoc 3.x" or higher
which grpc_cpp_plugin     # Should show a path
cmake --version           # Should show 3.x
python3 --version         # Should show 3.x
```

> [!TIP]
> If `apt` versions of gRPC are too old and the build fails with "undefined symbol" errors, install gRPC from source:
> ```bash
> git clone --recurse-submodules -b v1.62.0 https://github.com/grpc/grpc
> cd grpc && mkdir -p cmake/build && cd cmake/build
> cmake -DgRPC_INSTALL=ON -DCMAKE_INSTALL_PREFIX=/usr/local ../.. \
>   && make -j$(nproc) && sudo make install
> cd ~
> ```

---

## 3. Get the Project + Dataset

### 3.1 Clone the project

```bash
cd ~
git clone <your-repo-url> mini2
cd mini2
```

### 3.2 Get the dataset

The dataset file `dataset/311_2020.csv` (12.7 GB) must be inside WSL.

**Option A — USB drive:**
Plug in the USB drive. In WSL, USB drives appear under `/mnt/`:
```bash
# List available drives
ls /mnt/

# Copy from USB (adjust drive letter)
mkdir -p ~/mini2/dataset
cp /mnt/d/311_2020.csv ~/mini2/dataset/
# or
cp /mnt/e/311_2020.csv ~/mini2/dataset/
```

**Option B — Copy from Windows Downloads folder:**
```bash
cp /mnt/c/Users/<YourWindowsUsername>/Downloads/311_2020.csv ~/mini2/dataset/
```

**Option C — Teammate sends via network:**
```bash
# Your teammate (on Mac) can scp it to you if SSH is enabled in WSL
# On the Mac:
scp dataset/311_2020.csv <your-wsl-user>@<WINDOWS_IP>:~/mini2/dataset/
```

> [!WARNING]
> Do NOT keep the project on `/mnt/c/` (the Windows filesystem). WSL file I/O on `/mnt/c` is **extremely slow** — the dataset load will take 10× longer. Always work in `~/` (the WSL home directory).

### 3.3 Partition the dataset

```bash
cd ~/mini2
python3 scripts/partition_data.py
```

This takes ~5-10 minutes and creates 9 files in `data/partitions/`.

---

## 4. Build & Verify

### 4.1 Build the C++ binary

```bash
cd ~/mini2

# The build script defaults to /opt/homebrew (macOS). Override for Linux:
MINI2_PREFIX=/usr bash scripts/build.sh
```

If cmake can't find gRPC with `/usr`, try:
```bash
MINI2_PREFIX=/usr/local bash scripts/build.sh
```

**Expected last line:**
```
[build] done. binaries in /path/to/mini2/build
```

### 4.2 Generate Python gRPC stubs

```bash
bash scripts/gen_python_stubs.sh
```

### 4.3 Verify everything

```bash
# Binary exists?
ls -la build/mini2_server

# Python stubs exist?
ls python/generated/mini2_pb2.py

# All 9 partitions exist?
ls data/partitions/*.csv | wc -l
# Should show: 9
```

---

## 5. Network Setup (Critical)

This is the most important section. WSL 2 uses a virtual network, so extra steps are needed for the Mac to reach your servers.

### 5.1 Find your Windows IP

Open **PowerShell** (not WSL):
```powershell
ipconfig
```

Look for the **IPv4 Address** under your active adapter (Wi-Fi or Ethernet):
```
Wireless LAN adapter Wi-Fi:
   IPv4 Address. . . . . . . . . . . : 192.168.1.101    ← THIS ONE
```

**Tell your teammate this IP** — they need it for the config files.

### 5.2 Set up WSL port forwarding

WSL runs inside a virtual network. Ports opened in WSL are NOT reachable from other machines by default. You must forward them.

Open **PowerShell as Administrator**:

```powershell
# Get WSL's internal IP
$wslIP = (wsl hostname -I).Trim()
echo "WSL IP is: $wslIP"

# Forward ports 50055-50059 from Windows host to WSL
foreach ($port in 50055..50059) {
    netsh interface portproxy add v4tov4 listenport=$port listenaddress=0.0.0.0 connectport=$port connectaddress=$wslIP
}

echo "Port forwarding configured!"

# Verify:
netsh interface portproxy show v4tov4
```

You should see 5 entries (ports 50055–50059) forwarding to the WSL IP.

> [!CAUTION]
> The WSL IP **changes every time you restart WSL or Windows**. You must re-run this port forwarding after every reboot!

### 5.3 Open Windows Firewall

Still in **PowerShell as Administrator**:

```powershell
# Allow gRPC ports through Windows Firewall
New-NetFirewallRule -DisplayName "Mini2 gRPC" -Direction Inbound -Protocol TCP -LocalPort 50055-50059 -Action Allow
```

### 5.4 Test connectivity

Ask your teammate (on the Mac) to run:
```bash
nc -zv <YOUR_WINDOWS_IP> 50055
```

If it shows `Connection to ... succeeded!` — you're ready! If not, see Troubleshooting §9.

---

## 6. Launch Your Nodes

### 6.1 Get the LAN config files

Your teammate generates these on the Mac. Make sure you have the `config/tree-lan/` directory with files E.conf through I.conf that use **real IPs** (not 127.0.0.1).

If you don't have them yet, ask your teammate to run the config generation script from the main DEMO_GUIDE (§4.2), then copy the configs to you:
```bash
# Quick way — just copy the 9 config files (tiny, < 1 KB each)
scp teammate@MAC_IP:/path/to/mini2/config/tree-lan/* ~/mini2/config/tree-lan/
```

### 6.2 Start your nodes

In the **WSL terminal**:

```bash
cd ~/mini2

# Kill anything old
bash scripts/kill_all.sh

mkdir -p experiments/logs

# Start C++ nodes E, F, G, H
for node in E F G H; do
  ./build/mini2_server config/tree-lan/${node}.conf \
    > experiments/logs/${node}.log 2>&1 &
  echo $! > experiments/logs/${node}.pid
  echo "Started $node"
done

# Start Python node I
python3 python/server.py config/tree-lan/I.conf \
  > experiments/logs/I.log 2>&1 &
echo $! > experiments/logs/I.pid
echo "Started I (Python)"
```

### 6.3 Verify your nodes are running

```bash
# Should show 5 processes (4 C++ + 1 Python)
ps aux | grep -E "mini2_server|python.*server" | grep -v grep | wc -l
```

Wait **30 seconds** for dataset loading, then check logs:
```bash
# Check any node's log
tail -5 experiments/logs/E.log
```

You should see heartbeat messages. If you see errors, check §9.

---

## 7. Verify Everything Works

Once **both machines** have their nodes running:

1. Your teammate runs a query from the Mac:
   ```bash
   python3 python/client.py 127.0.0.1:50051 borough eq MANHATTAN 1000
   ```

2. Watch your logs for activity:
   ```bash
   # Should see PeerQuery and chunk push activity
   tail -f experiments/logs/E.log
   ```

3. The query should return ~4.1 million records (from all 9 nodes combined).

---

## 8. Shutting Down

### 8.1 Stop your nodes

```bash
bash scripts/kill_all.sh
```

### 8.2 Clean up networking (PowerShell as Administrator)

```powershell
# Remove firewall rule
Remove-NetFirewallRule -DisplayName "Mini2 gRPC"

# Remove port forwarding
netsh interface portproxy reset

echo "Cleanup done!"
```

---

## 9. Troubleshooting

### 9.1 `wsl: command not found`

Your Windows version is too old. You need Windows 10 version 2004+ or Windows 11.
- Run `winver` to check your version
- Update Windows if needed

### 9.2 Build fails: CMake can't find gRPC

```bash
# Try different prefix paths:
MINI2_PREFIX=/usr bash scripts/build.sh
# or
MINI2_PREFIX=/usr/local bash scripts/build.sh

# If neither works, check where gRPC is installed:
find /usr -name "grpc_cpp_plugin" 2>/dev/null
dpkg -L libgrpc++-dev | head -20
```

### 9.3 Port forwarding not working (Mac can't reach your ports)

**Re-run the forwarding** (WSL IP changes on restart):
```powershell
# PowerShell as Admin:
$wslIP = (wsl hostname -I).Trim()
echo "WSL IP: $wslIP"

# Clear old rules and re-add
netsh interface portproxy reset
foreach ($port in 50055..50059) {
    netsh interface portproxy add v4tov4 listenport=$port listenaddress=0.0.0.0 connectport=$port connectaddress=$wslIP
}
netsh interface portproxy show v4tov4
```

**Test from inside WSL** that the server is actually listening:
```bash
# Check if port 50055 is open locally
ss -tlnp | grep 50055
```

**Test from Windows** (PowerShell):
```powershell
Test-NetConnection -ComputerName localhost -Port 50055
```

### 9.4 `No module named 'grpc'`

```bash
pip3 install grpcio grpcio-tools protobuf
bash scripts/gen_python_stubs.sh
```

### 9.5 Dataset load is extremely slow

You're probably running from `/mnt/c/`. Move the project to WSL's native filesystem:
```bash
mv /mnt/c/Users/.../mini2 ~/mini2
cd ~/mini2
```

### 9.6 Nodes crash on startup

Check the log for the crashed node:
```bash
cat experiments/logs/E.log
```

Common causes:
- **Missing partition file**: Run `python3 scripts/partition_data.py`
- **Port in use**: Run `bash scripts/kill_all.sh`, then retry
- **Wrong config IP**: Make sure your LAN config uses real IPs, not 127.0.0.1

### 9.7 Everything was working, then stopped after reboot

WSL's IP changes on every reboot. **You must re-run port forwarding:**
```powershell
# PowerShell as Admin — copy-paste this every time after reboot:
$wslIP = (wsl hostname -I).Trim()
foreach ($port in 50055..50059) {
    netsh interface portproxy add v4tov4 listenport=$port listenaddress=0.0.0.0 connectport=$port connectaddress=$wslIP
}
New-NetFirewallRule -DisplayName "Mini2 gRPC" -Direction Inbound -Protocol TCP -LocalPort 50055-50059 -Action Allow -ErrorAction SilentlyContinue
echo "Ready! WSL IP: $wslIP"
```

---

## Quick-Reference Commands

```bash
# === Run these in WSL terminal ===

# Build
MINI2_PREFIX=/usr bash scripts/build.sh

# Generate Python stubs
bash scripts/gen_python_stubs.sh

# Start your 5 nodes (E,F,G,H,I)
for node in E F G H; do
  ./build/mini2_server config/tree-lan/${node}.conf > experiments/logs/${node}.log 2>&1 &
done
python3 python/server.py config/tree-lan/I.conf > experiments/logs/I.log 2>&1 &

# Check running processes
ps aux | grep -E "mini2_server|python.*server" | grep -v grep

# Stop all nodes
bash scripts/kill_all.sh

# Check logs
tail -20 experiments/logs/E.log
grep -i "error\|fail" experiments/logs/*.log
```

```powershell
# === Run these in PowerShell as Administrator ===

# Port forwarding (re-run after every reboot!)
$wslIP = (wsl hostname -I).Trim()
foreach ($port in 50055..50059) {
    netsh interface portproxy add v4tov4 listenport=$port listenaddress=0.0.0.0 connectport=$port connectaddress=$wslIP
}

# Firewall rule
New-NetFirewallRule -DisplayName "Mini2 gRPC" -Direction Inbound -Protocol TCP -LocalPort 50055-50059 -Action Allow

# Cleanup after demo
Remove-NetFirewallRule -DisplayName "Mini2 gRPC"
netsh interface portproxy reset
```
