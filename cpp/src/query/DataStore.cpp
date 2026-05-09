#include "query/DataStore.hpp"
#include "model/CSVParser.hpp"
#include <iostream>

namespace mini2 {

void DataStore::load(const std::string& csv_path) {
  std::cout << "[datastore] loading " << csv_path << "...\n";
  records_ = CSVParser::parse(csv_path);
  std::cout << "[datastore] loaded " << records_.size() << " records.\n";
}

} // namespace mini2
