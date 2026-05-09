#include "model/CSVParser.hpp"
#include <fstream>
#include <stdexcept>
#include <iostream>

namespace mini2 {

std::vector<std::string> CSVParser::split_csv_line(const std::string& line) {
  std::vector<std::string> fields;
  std::string current;
  bool in_quotes = false;

  for (size_t i = 0; i < line.size(); ++i) {
    char c = line[i];

    if (in_quotes) {
      if (c == '"') {
        if (i + 1 < line.size() && line[i + 1] == '"') {
          current += '"';
          ++i;
        } else {
          in_quotes = false;
        }
      } else {
        current += c;
      }
    } else {
      if (c == '"') {
        in_quotes = true;
      } else if (c == ',') {
        fields.push_back(current);
        current.clear();
      } else {
        current += c;
      }
    }
  }
  fields.push_back(current);
  return fields;
}

time_t CSVParser::parse_date(const std::string& date_str) {
  if (date_str.empty()) return 0;

  struct tm tm = {};
  // Try MM/DD/YYYY HH:MM:SS AM/PM format
  if (date_str.size() >= 10 && date_str[2] == '/') {
    tm.tm_mon = std::stoi(date_str.substr(0, 2)) - 1;
    tm.tm_mday = std::stoi(date_str.substr(3, 2));
    tm.tm_year = std::stoi(date_str.substr(6, 4)) - 1900;

    if (date_str.size() >= 19) {
      tm.tm_hour = std::stoi(date_str.substr(11, 2));
      tm.tm_min = std::stoi(date_str.substr(14, 2));
      tm.tm_sec = std::stoi(date_str.substr(17, 2));

      if (date_str.size() >= 22) {
        std::string ampm = date_str.substr(20, 2);
        if (ampm == "PM" && tm.tm_hour != 12) tm.tm_hour += 12;
        if (ampm == "AM" && tm.tm_hour == 12) tm.tm_hour = 0;
      }
    }
    tm.tm_isdst = -1;
    return mktime(&tm);
  }
  return 0;
}

int64_t CSVParser::safe_parse_int64(const std::string& s) {
  if (s.empty()) return 0;
  try { return std::stoll(s); } catch (...) { return 0; }
}

int32_t CSVParser::safe_parse_int32(const std::string& s) {
  if (s.empty()) return 0;
  try { return static_cast<int32_t>(std::stol(s)); } catch (...) { return 0; }
}

double CSVParser::safe_parse_double(const std::string& s) {
  if (s.empty()) return 0.0;
  try { return std::stod(s); } catch (...) { return 0.0; }
}

ServiceRecord CSVParser::parse_line(const std::vector<std::string>& fields) {
  ServiceRecord sr;
  auto getField = [&fields](size_t idx) -> const std::string& {
    static const std::string empty;
    return (idx < fields.size()) ? fields[idx] : empty;
  };

  // Mini 1 standard 311 mappings (based on headers in dataset)
  sr.unique_key = safe_parse_int64(getField(0));
  sr.created_date = parse_date(getField(1));
  sr.closed_date = parse_date(getField(2));
  sr.agency = getField(3);
  // skipped agency_name
  sr.complaint_type = getField(5);
  sr.descriptor = getField(6);
  // skipped many
  sr.incident_zip = safe_parse_int32(getField(9));
  sr.incident_address = getField(10);
  sr.city = getField(17);
  sr.status = stringToStatus(getField(20));
  sr.due_date = parse_date(getField(21));
  sr.resolution_updated_date = parse_date(getField(23));
  sr.borough = stringToBorough(getField(28));
  sr.x_coordinate = safe_parse_int32(getField(29));
  sr.y_coordinate = safe_parse_int32(getField(30));
  sr.channel_type = stringToChannelType(getField(31));
  sr.latitude = safe_parse_double(getField(41));
  sr.longitude = safe_parse_double(getField(42));

  return sr;
}

std::vector<ServiceRecord> CSVParser::parse(const std::string& path) {
  std::vector<ServiceRecord> records;
  std::ifstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error("Cannot open CSV file: " + path);
  }

  std::string line;
  if (!std::getline(file, line)) { // skip header
    return records;
  }

  while (std::getline(file, line)) {
    if (line.empty()) continue;
    auto fields = split_csv_line(line);
    records.push_back(parse_line(fields));
  }

  return records;
}

} // namespace mini2
