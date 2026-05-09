#include "model/ServiceRequest.hpp"
#include <algorithm>

namespace mini2 {

static std::string toUpper(const std::string& s) {
  std::string result = s;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  return result;
}

Borough stringToBorough(const std::string& s) {
  std::string u = toUpper(s);
  if (u == "MANHATTAN") return Borough::MANHATTAN;
  if (u == "BRONX") return Borough::BRONX;
  if (u == "BROOKLYN") return Borough::BROOKLYN;
  if (u == "QUEENS") return Borough::QUEENS;
  if (u == "STATEN ISLAND" || u == "STATEN_ISLAND") return Borough::STATEN_ISLAND;
  return Borough::UNSPECIFIED;
}

Status stringToStatus(const std::string& s) {
  std::string u = toUpper(s);
  if (u == "OPEN") return Status::OPEN;
  if (u == "CLOSED") return Status::CLOSED;
  if (u == "PENDING") return Status::PENDING;
  if (u == "IN PROGRESS" || u == "IN_PROGRESS") return Status::IN_PROGRESS;
  if (u == "ASSIGNED") return Status::ASSIGNED;
  return Status::OPEN;
}

ChannelType stringToChannelType(const std::string& s) {
  std::string u = toUpper(s);
  if (u == "PHONE") return ChannelType::PHONE;
  if (u == "ONLINE") return ChannelType::ONLINE;
  if (u == "MOBILE") return ChannelType::MOBILE;
  return ChannelType::OTHER;
}

mini2::ServiceRequest ServiceRecord::to_proto() const {
  mini2::ServiceRequest p;
  p.set_unique_key(unique_key);
  p.set_created_date(created_date);
  p.set_closed_date(closed_date);
  p.set_due_date(due_date);
  p.set_resolution_updated_date(resolution_updated_date);
  p.set_latitude(latitude);
  p.set_longitude(longitude);
  p.set_incident_zip(incident_zip);
  p.set_x_coordinate(x_coordinate);
  p.set_y_coordinate(y_coordinate);
  
  p.set_borough(static_cast<uint32_t>(borough));
  p.set_status(static_cast<uint32_t>(status));
  p.set_channel_type(static_cast<uint32_t>(channel_type));

  p.set_agency(agency);
  p.set_complaint_type(complaint_type);
  p.set_descriptor_(descriptor);
  p.set_incident_address(incident_address);
  p.set_city(city);

  return p;
}

ServiceRecord ServiceRecord::from_proto(const mini2::ServiceRequest& p) {
  ServiceRecord sr;
  sr.unique_key = p.unique_key();
  sr.created_date = p.created_date();
  sr.closed_date = p.closed_date();
  sr.due_date = p.due_date();
  sr.resolution_updated_date = p.resolution_updated_date();
  sr.latitude = p.latitude();
  sr.longitude = p.longitude();
  sr.incident_zip = p.incident_zip();
  sr.x_coordinate = p.x_coordinate();
  sr.y_coordinate = p.y_coordinate();

  sr.borough = static_cast<Borough>(p.borough());
  sr.status = static_cast<Status>(p.status());
  sr.channel_type = static_cast<ChannelType>(p.channel_type());

  sr.agency = p.agency();
  sr.complaint_type = p.complaint_type();
  sr.descriptor = p.descriptor_();
  sr.incident_address = p.incident_address();
  sr.city = p.city();

  return sr;
}

} // namespace mini2
