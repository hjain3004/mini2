#include "query/LocalQueryEngine.hpp"
#include <iostream>

namespace mini2 {

LocalQueryEngine::LocalQueryEngine(const DataStore& store, const IndexSet& index)
    : store_(store), index_(index) {}

bool LocalQueryEngine::evaluate_filter(const ServiceRecord& r, const QueryFilter& filter) const {
  if (filter.field_name() == "borough" && filter.op() == "eq") {
    return r.borough == stringToBorough(filter.value());
  }
  if (filter.field_name() == "complaint_type" && filter.op() == "eq") {
    return r.complaint_type == filter.value();
  }
  if (filter.field_name() == "created_date" && filter.op() == "between") {
    return r.created_date >= filter.start_int() && r.created_date <= filter.end_int();
  }
  if (filter.field_name() == "location" && filter.op() == "geo_bbox") {
    return r.latitude >= filter.lat_min() && r.latitude <= filter.lat_max() &&
           r.longitude >= filter.lon_min() && r.longitude <= filter.lon_max();
  }
  
  // Unindexed fields
  if (filter.field_name() == "agency" && filter.op() == "eq") {
    return r.agency == filter.value();
  }
  if (filter.field_name() == "status" && filter.op() == "eq") {
    return r.status == stringToStatus(filter.value());
  }

  // Unknown filter -> accept all for now, or could reject. Let's accept if empty.
  if (filter.field_name().empty()) return true;

  return false;
}

std::vector<ServiceRecord> LocalQueryEngine::run(const QueryFilter& filter, bool force_linear) const {
  std::vector<ServiceRecord> results;
  
  bool used_index = false;
  std::vector<uint32_t> candidate_rows;
  
  if (!force_linear) {
    candidate_rows = index_.lookup(filter, used_index);
  }

  if (used_index && !force_linear) {
    // We got candidate rows from the index. We still must evaluate the filter on them 
    // to filter out false positives (e.g. coarse geo_bbox index).
    // The created_date index also might have dupes if not careful, though shouldn't.
    results.reserve(candidate_rows.size());
    for (uint32_t row_id : candidate_rows) {
      const auto& r = store_.get(row_id);
      if (evaluate_filter(r, filter)) {
        results.push_back(r);
      }
    }
  } else {
    // Linear scan
    const auto& records = store_.records();
    for (const auto& r : records) {
      if (evaluate_filter(r, filter)) {
        results.push_back(r);
      }
    }
  }

  return results;
}

} // namespace mini2
