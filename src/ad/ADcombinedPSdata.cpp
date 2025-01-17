#include<chimbuko/ad/ADcombinedPSdata.hpp>
#include<chimbuko/util/serialize.hpp>

using namespace chimbuko;

std::string ADcombinedPSdata::serialize_cerealpb() const{
  return cereal_serialize(*this);
}

void ADcombinedPSdata::deserialize_cerealpb(const std::string &strstate){
  cereal_deserialize(*this, strstate);
}

std::string ADcombinedPSdata::net_serialize() const{
  return serialize_cerealpb();
}

void ADcombinedPSdata::net_deserialize(const std::string &s){
  deserialize_cerealpb(s);
}

std::pair<size_t, size_t> ADcombinedPSdata::send(ADNetClient &net_client) const{
  PerfTimer timer;
  timer.start();

  if (!net_client.use_ps())
    return std::make_pair(0, 0);

  int step = m_func_stats.getAnomalyData().get_step();
  if(m_counter_stats.getIOstep() != step) fatal_error("Step value mismatch between counter stats and func anomaly stats");
  if(m_anom_metrics.get_step() != step) fatal_error("Step value mismatch between anomaly metrics and func anomaly stats");

  Message msg;
  msg.set_info(net_client.get_client_rank(), net_client.get_server_rank(), MessageType::REQ_ADD, MessageKind::AD_PS_COMBINED_STATS, step);
  msg.set_msg(net_serialize());
  
  size_t sent_sz = msg.size();
  net_client.async_send(msg);
  size_t recv_sz =0;

  if(m_perf != nullptr){
    m_perf->add("ps_combine_send_update_ms", timer.elapsed_ms());
    m_perf->add("ps_combine_send_sent_MB", (double)sent_sz / 1000000.0); // MB
    m_perf->add("ps_combine_send_recv_MB", (double)recv_sz / 1000000.0); // MB
  }
  
  return std::make_pair(sent_sz, recv_sz);
}

