"""
datastore.py — Python implementation of DataStore, CSVParser, IndexSet, LocalQueryEngine for Node I
"""

import csv
import math
from datetime import datetime
from collections import defaultdict
import mini2_pb2

# Enum values mapping to proto
BOROUGH_MAP = {
    "MANHATTAN": 0, "BRONX": 1, "BROOKLYN": 2, "QUEENS": 3,
    "STATEN ISLAND": 4, "STATEN_ISLAND": 4
}
STATUS_MAP = {
    "OPEN": 0, "CLOSED": 1, "PENDING": 2, "IN PROGRESS": 3, "IN_PROGRESS": 3, "ASSIGNED": 4
}
CHANNEL_MAP = {
    "PHONE": 0, "ONLINE": 1, "MOBILE": 2
}

def parse_date(date_str: str) -> int:
    if not date_str:
        return 0
    try:
        dt = datetime.strptime(date_str, "%m/%d/%Y %I:%M:%S %p")
        return int(dt.timestamp())
    except ValueError:
        return 0

def safe_int(val: str, default=0) -> int:
    try:
        return int(float(val)) if val else default
    except ValueError:
        return default

def safe_float(val: str, default=0.0) -> float:
    try:
        return float(val) if val else default
    except ValueError:
        return default

class CSVParser:
    @staticmethod
    def parse(path: str) -> list[mini2_pb2.ServiceRequest]:
        records = []
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            reader = csv.reader(f)
            next(reader, None)  # skip header
            for row in reader:
                if not row: continue
                # Match C++ indices
                try:
                    sr = mini2_pb2.ServiceRequest()
                    sr.unique_key = safe_int(row[0])
                    sr.created_date = parse_date(row[1])
                    sr.closed_date = parse_date(row[2])
                    sr.agency = row[3]
                    sr.complaint_type = row[5]
                    sr.descriptor = row[6]
                    sr.incident_zip = safe_int(row[9])
                    sr.incident_address = row[10]
                    sr.city = row[17]
                    sr.status = STATUS_MAP.get(row[20].upper(), 0)
                    sr.due_date = parse_date(row[21])
                    sr.resolution_updated_date = parse_date(row[23])
                    sr.borough = BOROUGH_MAP.get(row[28].upper(), 5) # 5=UNSPECIFIED
                    sr.x_coordinate = safe_int(row[29])
                    sr.y_coordinate = safe_int(row[30])
                    sr.channel_type = CHANNEL_MAP.get(row[31].upper(), 3) # 3=OTHER
                    sr.latitude = safe_float(row[41])
                    sr.longitude = safe_float(row[42])
                    records.append(sr)
                except IndexError:
                    pass
        return records

class DataStore:
    def __init__(self):
        self.records = []
    
    def load(self, path: str):
        print(f"[datastore] loading {path}...")
        self.records = CSVParser.parse(path)
        print(f"[datastore] loaded {len(self.records)} records.")

class IndexSet:
    def __init__(self):
        self.borough_idx = defaultdict(list)
        self.complaint_idx = defaultdict(list)
        # Note: In Python, bisect on lists might be better than map, but for simplicity we just sort it later or use a dict
        self.created_idx = defaultdict(list)
        self.geo_idx = defaultdict(list)
        self.sorted_created_keys = []
    
    def get_geo_key(self, lat: float, lon: float) -> int:
        lat_b = int(round(lat * 100.0)) & 0xFFFFFFFF
        lon_b = int(round(lon * 100.0)) & 0xFFFFFFFF
        return (lat_b << 32) | lon_b

    def build(self, store: DataStore):
        print(f"[index] building indexes for {len(store.records)} records...")
        for i, r in enumerate(store.records):
            self.borough_idx[r.borough].append(i)
            self.complaint_idx[r.complaint_type].append(i)
            self.created_idx[r.created_date].append(i)
            if r.latitude != 0.0 and r.longitude != 0.0:
                self.geo_idx[self.get_geo_key(r.latitude, r.longitude)].append(i)
        
        self.sorted_created_keys = sorted(self.created_idx.keys())
        print(f"[index] built: borough={len(self.borough_idx)} complaint={len(self.complaint_idx)} dates={len(self.created_idx)} geo_cells={len(self.geo_idx)}")

    def lookup(self, filter_msg: mini2_pb2.QueryFilter) -> tuple[list[int], bool]:
        used_index = True
        results = []
        
        fname = filter_msg.field_name
        op = filter_msg.op

        if fname == "borough" and op == "eq":
            b = BOROUGH_MAP.get(filter_msg.value.upper(), 5)
            results = self.borough_idx.get(b, [])
        elif fname == "complaint_type" and op == "eq":
            results = self.complaint_idx.get(filter_msg.value, [])
        elif fname == "created_date" and op == "between":
            start = filter_msg.start_int
            end = filter_msg.end_int
            import bisect
            left = bisect.bisect_left(self.sorted_created_keys, start)
            right = bisect.bisect_right(self.sorted_created_keys, end)
            for k in self.sorted_created_keys[left:right]:
                results.extend(self.created_idx[k])
        elif fname == "location" and op == "geo_bbox":
            min_lat_b = int(round(filter_msg.lat_min * 100.0))
            max_lat_b = int(round(filter_msg.lat_max * 100.0))
            min_lon_b = int(round(filter_msg.lon_min * 100.0))
            max_lon_b = int(round(filter_msg.lon_max * 100.0))
            
            for lat in range(min_lat_b, max_lat_b + 1):
                for lon in range(min_lon_b, max_lon_b + 1):
                    key = ((lat & 0xFFFFFFFF) << 32) | (lon & 0xFFFFFFFF)
                    if key in self.geo_idx:
                        results.extend(self.geo_idx[key])
        else:
            used_index = False
            
        return results, used_index

class LocalQueryEngine:
    def __init__(self, store: DataStore, index: IndexSet):
        self.store = store
        self.index = index

    def _evaluate_filter(self, r: mini2_pb2.ServiceRequest, filter_msg: mini2_pb2.QueryFilter) -> bool:
        fname = filter_msg.field_name
        op = filter_msg.op
        
        if fname == "borough" and op == "eq":
            return r.borough == BOROUGH_MAP.get(filter_msg.value.upper(), 5)
        if fname == "complaint_type" and op == "eq":
            return r.complaint_type == filter_msg.value
        if fname == "created_date" and op == "between":
            return filter_msg.start_int <= r.created_date <= filter_msg.end_int
        if fname == "location" and op == "geo_bbox":
            return (filter_msg.lat_min <= r.latitude <= filter_msg.lat_max and
                    filter_msg.lon_min <= r.longitude <= filter_msg.lon_max)
        if fname == "agency" and op == "eq":
            return r.agency == filter_msg.value
        if fname == "status" and op == "eq":
            return r.status == STATUS_MAP.get(filter_msg.value.upper(), 0)
        
        if not fname:
            return True
            
        return False

    def run(self, filter_msg: mini2_pb2.QueryFilter, force_linear: bool = False) -> list[mini2_pb2.ServiceRequest]:
        used_index = False
        candidate_rows = []
        
        if not force_linear:
            candidate_rows, used_index = self.index.lookup(filter_msg)
            
        results = []
        if used_index and not force_linear:
            for row_id in candidate_rows:
                r = self.store.records[row_id]
                if self._evaluate_filter(r, filter_msg):
                    results.append(r)
        else:
            for r in self.store.records:
                if self._evaluate_filter(r, filter_msg):
                    results.append(r)
                    
        return results
