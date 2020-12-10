#include <chimbuko/ad/ADLocalFuncStatistics.hpp>
#include <chimbuko/ad/AnomalyData.hpp>

#include <cereal/archives/portable_binary.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/string.hpp>

using namespace chimbuko;

std::string ADLocalFuncStatistics::State::serialize_cerealpb() const{
  std::stringstream ss;
  {    
    cereal::PortableBinaryOutputArchive wr(ss);
    wr(*this);    
  }
  return ss.str();
}

void ADLocalFuncStatistics::State::deserialize_cerealpb(const std::string &strstate){
  std::stringstream ss; ss << strstate;;
  {    
    cereal::PortableBinaryInputArchive rd(ss);
    rd(*this);    
  }
}

ADLocalFuncStatistics::ADLocalFuncStatistics(const unsigned long program_idx, const unsigned long rank, const int step, PerfStats* perf): 
  m_program_idx(program_idx), m_rank(rank), m_step(step), m_min_ts(0), m_max_ts(0), m_perf(perf), m_n_anomalies(0){}


void ADLocalFuncStatistics::gatherStatistics(const ExecDataMap_t* exec_data){
  for (auto it : *exec_data) { //loop over functions (key is function index)
    unsigned long func_id = it.first;
    for (auto itt : it.second) { //loop over events for that function
      m_func[func_id] = itt->get_funcname();
      m_inclusive[func_id].push(static_cast<double>(itt->get_inclusive()));
      m_exclusive[func_id].push(static_cast<double>(itt->get_exclusive()));

      if (m_min_ts == 0 || m_min_ts > itt->get_entry())
	m_min_ts = itt->get_entry();
      if (m_max_ts == 0 || m_max_ts < itt->get_exit())
	m_max_ts = itt->get_exit();      
    }
  }
}

void ADLocalFuncStatistics::gatherAnomalies(const Anomalies &anom){
  for(auto fit: m_func){
    unsigned long func_id = fit.first;
    m_anomaly_count[func_id] += anom.nFuncEvents(func_id, Anomalies::EventType::Outlier);
  }
  m_n_anomalies += anom.nEvents(Anomalies::EventType::Outlier);
}

nlohmann::json ADLocalFuncStatistics::get_json_state() const{
  // func id --> (name, # anomaly, inclusive run stats, exclusive run stats)
  nlohmann::json g_info;
  g_info["func"] = nlohmann::json::array();
  for (auto it : m_func) { //loop over function index
    const unsigned long func_id = it.first;
    const unsigned long n = m_anomaly_count.find(func_id)->second;

    nlohmann::json obj;
    obj["pid"] = m_program_idx;
    obj["id"] = func_id;
    obj["name"] = it.second;
    obj["n_anomaly"] = n;
    obj["inclusive"] = m_inclusive.find(func_id)->second.get_json_state();
    obj["exclusive"] = m_exclusive.find(func_id)->second.get_json_state();
    g_info["func"].push_back(obj);
  }

  g_info["anomaly"] = AnomalyData(m_program_idx, m_rank, m_step, m_min_ts, m_max_ts, m_n_anomalies).get_json();
  return g_info;
}


ADLocalFuncStatistics::State ADLocalFuncStatistics::get_state() const{
  // func id --> (name, # anomaly, inclusive run stats, exclusive run stats)
  State g_info;
  for (auto it : m_func) { //loop over function index
    const unsigned long func_id = it.first;
    State::FuncData obj;
    obj.pid = m_program_idx;
    obj.id = func_id;
    obj.name = it.second;
    obj.n_anomaly = m_anomaly_count.find(func_id)->second;
    obj.inclusive = m_inclusive.find(func_id)->second.get_state();
    obj.exclusive = m_exclusive.find(func_id)->second.get_state();
    g_info.func.push_back(obj);
  }

  g_info.anomaly = AnomalyData(m_program_idx, m_rank, m_step, m_min_ts, m_max_ts, m_n_anomalies);
  return g_info;
}


std::pair<size_t, size_t> ADLocalFuncStatistics::updateGlobalStatistics(ADNetClient &net_client) const{
  // func id --> (name, # anomaly, inclusive run stats, exclusive run stats)
  //nlohmann::json g_info = get_json_state();
  State g_info = get_state();
  PerfTimer timer;
  timer.start();
  //auto msgsz = updateGlobalStatistics(net_client, g_info.dump(), m_step);
  auto msgsz = updateGlobalStatistics(net_client, g_info.serialize_cerealpb(), m_step);
  
  if(m_perf != nullptr){
    m_perf->add("func_stats_stream_update_us", timer.elapsed_us());
    m_perf->add("func_stats_stream_sent_MB", (double)msgsz.first / 1000000.0); // MB
    m_perf->add("func_stats_stream_recv_MB", (double)msgsz.second / 1000000.0); // MB
  }
  
  return msgsz;
}

std::pair<size_t, size_t> ADLocalFuncStatistics::updateGlobalStatistics(ADNetClient &net_client, const std::string &l_stats, int step){
  if (!net_client.use_ps())
    return std::make_pair(0, 0);
  
  Message msg;
  msg.set_info(net_client.get_client_rank(), net_client.get_server_rank(), MessageType::REQ_ADD, MessageKind::ANOMALY_STATS, step);
  msg.set_msg(l_stats);
  
  size_t sent_sz = msg.size();
  std::string strmsg = net_client.send_and_receive(msg);
  size_t recv_sz = strmsg.size();
  
  return std::make_pair(sent_sz, recv_sz);
}
