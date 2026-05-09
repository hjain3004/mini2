#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <memory>
#include "mini2.pb.h"

namespace mini2 {

// ─── Enums ──────────────────────────────────────────────────────────────────

enum class Borough : uint8_t {
  MANHATTAN = 0,
  BRONX = 1,
  BROOKLYN = 2,
  QUEENS = 3,
  STATEN_ISLAND = 4,
  UNSPECIFIED = 5
};

enum class Status : uint8_t {
  OPEN = 0,
  CLOSED = 1,
  PENDING = 2,
  IN_PROGRESS = 3,
  ASSIGNED = 4
};

enum class ChannelType : uint8_t {
  PHONE = 0,
  ONLINE = 1,
  MOBILE = 2,
  OTHER = 3
};

Borough stringToBorough(const std::string& s);
Status stringToStatus(const std::string& s);
ChannelType stringToChannelType(const std::string& s);

// ─── ServiceRequest ─────────────────────────────────────────────────────────

struct ServiceRecord {
  int64_t unique_key = 0;
  time_t created_date = 0;
  time_t closed_date = 0;
  time_t due_date = 0;
  time_t resolution_updated_date = 0;
  double latitude = 0.0;
  double longitude = 0.0;
  int32_t incident_zip = 0;
  int32_t x_coordinate = 0;
  int32_t y_coordinate = 0;
  
  Borough borough = Borough::UNSPECIFIED;
  Status status = Status::OPEN;
  ChannelType channel_type = ChannelType::OTHER;

  std::string agency;
  std::string complaint_type;
  std::string descriptor;
  std::string incident_address;
  std::string city;

  // Conversion to/from Protobuf
  mini2::ServiceRequest to_proto() const;
  static ServiceRecord from_proto(const mini2::ServiceRequest& p);
};

} // namespace mini2
