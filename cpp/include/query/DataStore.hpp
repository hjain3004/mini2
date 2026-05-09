#pragma once

#include "model/ServiceRequest.hpp"
#include <vector>
#include <string>

namespace mini2 {

/**
 * DataStore — simple wrapper around vector<ServiceRequest>.
 */
class DataStore {
public:
  DataStore() = default;

  void load(const std::string& csv_path);

  const ServiceRecord& get(size_t row_id) const {
    return records_[row_id];
  }

  size_t size() const {
    return records_.size();
  }

  const std::vector<ServiceRecord>& records() const {
    return records_;
  }

private:
  std::vector<ServiceRecord> records_;
};

} // namespace mini2
