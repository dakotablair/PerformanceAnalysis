#include "chimbuko/param/sstd_param.hpp"
#include <sstream>
#include <cereal/archives/portable_binary.hpp>
#include <cereal/types/unordered_map.hpp>

using namespace chimbuko;

SstdParam::SstdParam()
{
    clear();
}

SstdParam::~SstdParam()
{

}

std::string SstdParam::serialize() const
{
    std::lock_guard<std::mutex> _{m_mutex};
    //return serialize_json(m_runstats);
    return serialize_cerealpb(m_runstats);
}

std::string SstdParam::serialize_json(const std::unordered_map<unsigned long, RunStats>& runstats)
{
    nlohmann::json j;
    for (auto& pair: runstats)
    {
        j[std::to_string(pair.first)] = pair.second.get_json_state();
    }
    return j.dump();
}

void SstdParam::deserialize_json(
    const std::string& parameters,
    std::unordered_map<unsigned long, RunStats>& runstats)
{
    nlohmann::json j = nlohmann::json::parse(parameters);

    for (auto it = j.begin(); it != j.end(); it++)
    {
        unsigned long key = std::stoul(it.key());
        runstats[key] = RunStats::from_strstate(it.value().dump());
    }
}

std::string SstdParam::serialize_cerealpb(const std::unordered_map<unsigned long, RunStats>& runstats){
  std::unordered_map<unsigned long, RunStats::State> state;
  for(auto const& e : runstats)
    state[e.first] = e.second.get_state();
  std::stringstream ss;
  {
    cereal::PortableBinaryOutputArchive wr(ss);
    wr(state);
  }
  return ss.str();
}

void SstdParam::deserialize_cerealpb(const std::string& parameters,  std::unordered_map<unsigned long, RunStats>& runstats){
  std::stringstream ss; ss << parameters;
  std::unordered_map<unsigned long, RunStats::State> state;
  {
    cereal::PortableBinaryInputArchive rd(ss);
    rd(state);
  }
  for(auto const& e : state)
    runstats[e.first] = RunStats::from_state(e.second);
}


std::string SstdParam::update(const std::string& parameters, bool return_update)
{
    std::unordered_map<unsigned long, RunStats> runstats;
    //deserialize_json(parameters, runstats);
    deserialize_cerealpb(parameters, runstats);
    update_and_return(runstats); //update runstats to reflect changes
    //return (return_update) ? serialize_json(runstats): "";
    return (return_update) ? serialize_cerealpb(runstats): "";
}

void SstdParam::assign(const std::unordered_map<unsigned long, RunStats>& runstats)
{
    std::lock_guard<std::mutex> _(m_mutex);
    for (auto& pair: runstats) {
        m_runstats[pair.first] = pair.second;
    }
}

void SstdParam::assign(const std::string& parameters)
{
    std::unordered_map<unsigned long, RunStats> runstats;
    //deserialize_json(parameters, runstats);
    deserialize_cerealpb(parameters, runstats);
    assign(runstats);
}

void SstdParam::clear()
{
    m_runstats.clear();
}


void SstdParam::update(const std::unordered_map<unsigned long, RunStats>& runstats)
{
    std::lock_guard<std::mutex> _(m_mutex);
    for (auto& pair: runstats) {
        m_runstats[pair.first] += pair.second;
    }
}


void SstdParam::update_and_return(std::unordered_map<unsigned long, RunStats>& runstats)
{
    std::lock_guard<std::mutex> _(m_mutex);
    for (auto& pair: runstats) {
        m_runstats[pair.first] += pair.second;
        pair.second = m_runstats[pair.first];
    }
}

void SstdParam::show(std::ostream& os) const
{
    os << "\n"
       << "Parameters: " << m_runstats.size() << std::endl;

    for (auto stat: m_runstats)
    {
        os << "Function " << stat.first << std::endl;
        os << stat.second.get_json().dump(2) << std::endl;
    }

    os << std::endl;
}


const RunStats & SstdParam::get_function_stats(const unsigned long func_id) const{
  auto it = m_runstats.find(func_id);
  if(it == m_runstats.end()) throw std::runtime_error("Invalid function index in SstdParam::get_function_stats");
  return it->second;
}



/**
 * HBOS based implementation
 */
 HbosParam::HbosParam() {
   clear();
 }

 HbosParam::~HbosParam(){

 }

 void HbosParam::clear()
 {
     m_hbosstats.clear();
 }

 void HbosParam::show(std::ostream& os) const
 {
     os << "\n"
        << "Parameters: " << m_hbosstats.size() << std::endl;

     for (auto stat: m_hbosstats)
     {
         os << "Function " << stat.first << std::endl;
         os << stat.second.get_json().dump(2) << std::endl;
     }

     os << std::endl;
 }

 void HbosParam::assign(const std::unordered_map<unsigned long, Histogram>& hbosstats)
 {
     std::lock_guard<std::mutex> _(m_mutex);
     for (auto& pair: hbosstats) {
         m_hbosstats[pair.first] = pair.second;
     }
 }

 void SstdParam::assign(const std::string& parameters)
 {
     std::unordered_map<unsigned long, Histogram> hbosstats;
     //deserialize_json(parameters, runstats);
     deserialize_cerealpb(parameters, hbosstats);
     assign(hbosstats);
 }

 std::string HbosParam::serialize() const
 {
     std::lock_guard<std::mutex> _{m_mutex};

     return serialize_cerealpb(m_hbosstats);
 }

 std::string HbosParam::serialize_cerealpb(const std::unordered_map<unsigned long, Histogram>& hbosstats)
 {
   std::unordered_map<unsigned long, Histogram::Data> histdata;
   for(auto const& e : hbosstats)
     histdata[e.first] = e.second.get_histogram();
   std::stringstream ss;
   {
     cereal::PortableBinaryOutputArchive wr(ss);
     wr(histdata);
   }
   return ss.str();
 }

 void HbosParam::deserialize_cerealpb(const std::string& parameters,  std::unordered_map<unsigned long, Histogram>& hbosstats)
 {
   std::stringstream ss; ss << parameters;
   std::unordered_map<unsigned long, Histogram::Data> histdata;
   {
     cereal::PortableBinaryInputArchive rd(ss);
     rd(histdata);
   }
   for(auto const& e : histdata)
     hbosstats[e.first] = Histogram::from_hist_data(e.second);
 }

 std::string HbosParam::update(const std::string& parameters, bool return_update)
 {
     std::unordered_map<unsigned long, Histogram> hbosstats;

     deserialize_cerealpb(parameters, hbosstats);
     update_and_return(hbosstats); //update runstats to reflect changes

     return (return_update) ? serialize_cerealpb(hbosstats): "";
 }

 void HbosParam::update(const std::unordered_map<unsigned long, Histogram>& hbosstats)
 {
     std::lock_guard<std::mutex> _(m_mutex);
     for (auto& pair: hbosstats) {
         m_hbosstats[pair.first] += pair.second;
     }
 }

 void HbosParam::update_and_return(std::unordered_map<unsigned long, Histogram>& hbosstats)
 {
     std::lock_guard<std::mutex> _(m_mutex);
     for (auto& pair: hbosstats) {
         m_hbosstats[pair.first] += pair.second;
         pair.second = m_hbosstats[pair.first];
     }
 }

 nlohmann::json & HbosParam::get_function_stats(const unsigned long func_id) const{
   auto it = m_hbosstats.find(func_id);
   if(it == m_hbosstats.end()) throw std::runtime_error("Invalid function index in SstdParam::get_function_stats");
   return it->second.get_json();
 }


 /**
  * @brief Histogram Class Implementation
  */

  /**
   * @brief Merge Histogram
   */
 Histogram chimbuko::operator+(const Histogram g, const Histogram l)
 {
   Histogram combined;
   if (g.bin_edges.size() == 0) {
     combined.runtimes = l.runtimes;
     combined.counts = l.counts;
     combined.bin_edges = l.bin_edges;
   }
   else if (l.bin_edges.size() == 0) {
     combined.runtimes = g.runtimes;
     combined.counts = g.counts;
     combined.bin_edges = g.bin_edges;
   }
   else {
     //unwrap both histograms into values
     std::vector<double> runtimes;

     for (int i = 0; i < g.bin_edges.size() - 1; i++) {
       for(int j = 0; j < g.counts.at(i); j++){
         runtimes.push_back(g.bin_edges.at(i));
       }
     }
     for (int i = 0; i < l.bin_edges.size() - 1; i++) {
       for(int j = 0; j < l.counts.at(i); j++){
         runtimes.push_back(l.bin_edges.at(i));
       }
     }

     double bin_width = Histogram::_scott_binWidth(runtimes);
     std::sort(runtimes.begin(), runtimes.end());
     int h = runtimes.size() - 1;

     combined.bin_edges.push_back(runtimes.at(0));

     double prev = combined.bin_edges.at(0);
     while(prev < runtimes.at(h)){
       combined.bin_edges.push_back(prev + bin_width);
       prev += bin_width;
     }
     VERBOSE(std::cout << "Number of bins: " << combined.bin_edges.size()-1 << std::endl);

     combined.counts = std::vector<double>(combined.bin_edges.size()-1, 0.0);
     for ( int i=0; i < runtimes.size(); i++) {
       for ( int j=1; j < combined.bin_edges.size(); j++) {
         if ( runtimes.at(i) < combined.bin_edges.at(j) ) {
           combined.counts[j-1] += 1;
           break;
         }
       }
     }
     VERBOSE(std::cout << "Size of counts: " << combined.counts.size() << std::endl);

     combined.set_hist_data(Histogram::Data( combined.runtimes, combined.counts, combined.bin_edges ));

     return combined;
   }
 }

 Histogram& Histogram::operator+=(const Histogram& h)
 {
    Histogram combined = *this + h;
    *this = combined;
    return *this;
 }

 double Histogram::_scott_binWidth(std::vector<double> vals){
   //Find bin width as per Scott's rule = 3.5*std*n^-1/3

   double sum = std::accumulate(vals.begin(), vals.end(), 0.0);

   double mean = sum / vals.size();
   double var;
   for(int i=0; i<vals.size(); i++){
     var += pow(vals.at(i) - mean, 2);
   }
   var = var / vals.size();

   return (3.5 * sqrt(var) ) / pow(vals.size(), 1/3);
 }

 void Histogram::set_hist_data(const Histogram::Data& d)
 {
     m_histogram.runtimes = d.runtimes;
     m_histogram.counts = d.counts;
     m_histogram.bin_edges = d.bin_edges;
 }

 void Histogram::push (double x)
 {
   m_histogram.runtimes.push_back(x);
 }

 void Histogram::create_histogram()
 {
   double bin_width = Histogram::_scott_binWidth(m_histogram.runtimes);
   std::sort(m_histogram.runtimes.begin(), m_histogram.runtimes.end());
   int h = m_histogram.runtimes.size() - 1;

   m_histogram.bin_edges.push_back(m_histogram.runtimes.at(0));

   double prev = m_histogram.bin_edges.at(0);
   while(prev < m_histogram.runtimes.at(h)){
     m_histogram.bin_edges.push_back(prev + bin_width);
     prev += bin_width;
   }
   VERBOSE(std::cout << "Number of bins: " << m_histogram.bin_edges.size()-1 << std::endl);

   m_histogram.counts = std::vector<double>(m_histogram.bin_edges.size()-1, 0.0);
   for ( int i=0; i < m_histogram.runtimes.size(); i++) {
     for ( int j=1; j < m_histogram.bin_edges.size(); j++) {
       if ( m_histogram.runtimes.at(i) < m_histogram.bin_edges.at(j) ) {
         m_histogram.counts[j-1] += 1;
         break;
       }
     }
   }
   VERBOSE(std::cout << "Size of counts: " << m_histogram.counts.size() << std::endl);
 }

 nlohmann::json Histogram::get_json() const {
     return {
         {"Histogram", {
           {"Bin Counts", m_histogram.counts},
           {"Bin Edges", m_histogram.bin_edges}
         }
       };
 }
