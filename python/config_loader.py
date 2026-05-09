"""
config_loader.py — key=value config parser, mirrors C++ ConfigManager.
"""


class NeighborEntry:
    def __init__(self, node_id: str, host: str, port: int):
        self.id = node_id
        self.host = host
        self.port = port

    def __repr__(self):
        return f"{self.id}@{self.host}:{self.port}"


class Config:
    def __init__(self):
        self.node_id = ""
        self.role = ""
        self.language = ""
        self.host = ""
        self.port = 0
        self.dataset_path = ""
        self.overlay = ""
        self.neighbors: list[NeighborEntry] = []
        self.default_chunk_records = 500
        self.max_chunk_bytes = 65536
        self.adaptive_chunking = False
        self.scheduler_policy = "round_robin"
        self.max_active_requests = 32
        self.worker_pool_size = 8
        self.request_ttl_ms = 10000
        self.peer_timeout_ms = 3000
        self.client_poll_timeout_ms = 1000
        self.abandon_timeout_ms = 15000
        self.metrics_output_path = ""


def _parse_neighbor(token: str) -> NeighborEntry:
    """Parse 'B:127.0.0.1:50052' into a NeighborEntry."""
    parts = token.strip().split(":")
    if len(parts) != 3:
        raise ValueError(f"neighbor must be id:host:port, got: {token}")
    return NeighborEntry(parts[0].strip(), parts[1].strip(), int(parts[2].strip()))


def load_config(path: str) -> Config:
    """Load a key=value config file. Mirrors C++ ConfigManager::load()."""
    cfg = Config()
    with open(path, "r") as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if "=" not in line:
                raise ValueError(f"config line {lineno} missing '=': {line}")
            key, val = line.split("=", 1)
            key = key.strip()
            val = val.strip()

            if key == "node_id":
                cfg.node_id = val
            elif key == "role":
                cfg.role = val
            elif key == "language":
                cfg.language = val
            elif key == "host":
                cfg.host = val
            elif key == "port":
                cfg.port = int(val)
            elif key == "dataset_path":
                cfg.dataset_path = val
            elif key == "overlay":
                cfg.overlay = val
            elif key == "neighbors":
                if val:
                    cfg.neighbors = [_parse_neighbor(t) for t in val.split(",") if t.strip()]
            elif key == "default_chunk_records":
                cfg.default_chunk_records = int(val)
            elif key == "max_chunk_bytes":
                cfg.max_chunk_bytes = int(val)
            elif key == "adaptive_chunking":
                cfg.adaptive_chunking = val.lower() in ("true", "1")
            elif key == "scheduler_policy":
                cfg.scheduler_policy = val
            elif key == "max_active_requests":
                cfg.max_active_requests = int(val)
            elif key == "worker_pool_size":
                cfg.worker_pool_size = int(val)
            elif key == "request_ttl_ms":
                cfg.request_ttl_ms = int(val)
            elif key == "peer_timeout_ms":
                cfg.peer_timeout_ms = int(val)
            elif key == "client_poll_timeout_ms":
                cfg.client_poll_timeout_ms = int(val)
            elif key == "abandon_timeout_ms":
                cfg.abandon_timeout_ms = int(val)
            elif key == "metrics_output_path":
                cfg.metrics_output_path = val
            # Unknown keys tolerated (forward-compatible)

    if not cfg.node_id:
        raise ValueError("config missing node_id")
    if not cfg.host:
        raise ValueError("config missing host")
    if cfg.port == 0:
        raise ValueError("config missing port")

    return cfg
