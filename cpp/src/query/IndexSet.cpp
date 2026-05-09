#include "query/IndexSet.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>

namespace mini2 {

uint64_t IndexSet::get_geo_key(double lat, double lon) const {
  int32_t lat_b = static_cast<int32_t>(std::round(lat * 100.0));
  int32_t lon_b = static_cast<int32_t>(std::round(lon * 100.0));
  return (static_cast<uint64_t>(lat_b) << 32) | (static_cast<uint32_t>(lon_b));
}

void IndexSet::build(const DataStore& store) {
  std::cout << "[index] building indexes for " << store.size() << " records...\n";
  const auto& records = store.records();
  
  for (uint32_t i = 0; i < records.size(); ++i) {
    const auto& r = records[i];
    
    borough_idx_[r.borough].push_back(i);
    complaint_idx_[r.complaint_type].push_back(i);
    created_idx_[r.created_date].push_back(i);
    
    if (r.latitude != 0.0 && r.longitude != 0.0) {
      geo_idx_[get_geo_key(r.latitude, r.longitude)].push_back(i);
    }
  }
  
  std::cout << "[index] built: borough=" << borough_idx_.size() 
            << " complaint=" << complaint_idx_.size() 
            << " dates=" << created_idx_.size()
            << " geo_cells=" << geo_idx_.size() << "\n";
}

std::vector<uint32_t> IndexSet::lookup(const QueryFilter& filter, bool& used_index) const {
  used_index = true;
  std::vector<uint32_t> results;

  if (filter.field_name() == "borough" && filter.op() == "eq") {
    auto b = stringToBorough(filter.value());
    auto it = borough_idx_.find(b);
    if (it != borough_idx_.end()) results = it->second;
  }
  else if (filter.field_name() == "complaint_type" && filter.op() == "eq") {
    auto it = complaint_idx_.find(filter.value());
    if (it != complaint_idx_.end()) results = it->second;
  }
  else if (filter.field_name() == "created_date" && filter.op() == "between") {
    auto start_it = created_idx_.lower_bound(filter.start_int());
    auto end_it = created_idx_.upper_bound(filter.end_int());
    for (auto it = start_it; it != end_it; ++it) {
      results.insert(results.end(), it->second.begin(), it->second.end());
    }
    // Ranges might not be perfectly sorted by row_id, but good enough for now.
    // If exact ordering is needed, we would sort the row ids here.
  }
  else if (filter.field_name() == "location" && filter.op() == "geo_bbox") {
    // Collect all cells that intersect the bbox
    int32_t min_lat_b = static_cast<int32_t>(std::round(filter.lat_min() * 100.0));
    int32_t max_lat_b = static_cast<int32_t>(std::round(filter.lat_max() * 100.0));
    int32_t min_lon_b = static_cast<int32_t>(std::round(filter.lon_min() * 100.0));
    int32_t max_lon_b = static_cast<int32_t>(std::round(filter.lon_max() * 100.0));
    
    for (int32_t lat = min_lat_b; lat <= max_lat_b; ++lat) {
      for (int32_t lon = min_lon_b; lon <= max_lon_b; ++lon) {
        uint64_t key = (static_cast<uint64_t>(lat) << 32) | (static_cast<uint32_t>(lon));
        auto it = geo_idx_.find(key);
        if (it != geo_idx_.end()) {
          results.insert(results.end(), it->second.begin(), it->second.end());
        }
      }
    }
  }
  else {
    used_index = false;
  }

  return results;
}

} // namespace mini2
