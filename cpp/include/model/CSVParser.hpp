#pragma once

#include "model/ServiceRequest.hpp"
#include <string>
#include <vector>

namespace mini2 {

class CSVParser {
public:
  static std::vector<ServiceRecord> parse(const std::string& path);

private:
  static ServiceRecord parse_line(const std::vector<std::string>& fields);
  static std::vector<std::string> split_csv_line(const std::string& line);
  
  static time_t parse_date(const std::string& date_str);
  static int64_t safe_parse_int64(const std::string& s);
  static int32_t safe_parse_int32(const std::string& s);
  static double safe_parse_double(const std::string& s);
};

} // namespace mini2
