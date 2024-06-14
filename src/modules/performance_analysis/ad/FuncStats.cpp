#include<chimbuko/modules/performance_analysis/ad/FuncStats.hpp>

using namespace chimbuko;
using namespace chimbuko::modules::performance_analysis;

nlohmann::json FuncStats::get_json() const{
  nlohmann::json obj;
  obj["pid"] = pid;
  obj["id"] = id;
  obj["name"] = name;
  obj["n_anomaly"] = n_anomaly;
  obj["inclusive"] = inclusive.get_json();
  obj["exclusive"] = exclusive.get_json();
  return obj;
}
