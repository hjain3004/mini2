#pragma once

#include "query/DataStore.hpp"
#include "mini2.pb.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <map>

namespace mini2 {

/**
 * IndexSet — Four secondary indexes over the DataStore.
 * Built once at boot time.
 */
class IndexSet {
public:
  IndexSet() = default;

  void build(const DataStore& store);

  // Returns matching row IDs, or an empty vector if no match.
  // Returns empty vector and sets `used_index = false` if the filter field isn't indexed.
  std::vector<uint32_t> lookup(const QueryFilter& filter, bool& used_index) const;

private:
  std::unordered_map<Borough, std::vector<uint32_t>> borough_idx_;
  std::unordered_map<std::string, std::vector<uint32_t>> complaint_idx_;
  std::map<time_t, std::vector<uint32_t>> created_idx_;
  
  // Coarse geo index (0.01 degree grid)
  // key: (lat_bucket << 32) | (lon_bucket & 0xFFFFFFFF)
  std::unordered_map<uint64_t, std::vector<uint32_t>> geo_idx_;
  
  uint64_t get_geo_key(double lat, double lon) const;
};

} // namespace mini2
