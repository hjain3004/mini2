#pragma once

#include "query/DataStore.hpp"
#include "query/IndexSet.hpp"
#include "mini2.pb.h"
#include <vector>

namespace mini2 {

/**
 * LocalQueryEngine — processes a query against the local DataStore/IndexSet.
 */
class LocalQueryEngine {
public:
  LocalQueryEngine(const DataStore& store, const IndexSet& index);

  // Run the query. If force_linear is true, bypasses the IndexSet.
  std::vector<ServiceRecord> run(const QueryFilter& filter, bool force_linear = false) const;

private:
  const DataStore& store_;
  const IndexSet& index_;

  bool evaluate_filter(const ServiceRecord& r, const QueryFilter& filter) const;
};

} // namespace mini2
