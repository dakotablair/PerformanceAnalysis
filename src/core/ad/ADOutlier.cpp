#include "chimbuko/core/ad/ADOutlier.hpp"
#include "chimbuko/core/param/sstd_param.hpp"
#include "chimbuko/core/param/hbos_param.hpp"
#include "chimbuko/core/param/copod_param.hpp"
#include "chimbuko/core/message.hpp"
#include "chimbuko/core/verbose.hpp"
#include "chimbuko/core/util/error.hpp"
#include <mpi.h>
#include <nlohmann/json.hpp>
#include <boost/math/distributions/normal.hpp>
#include <boost/math/distributions/empirical_cumulative_distribution_function.hpp>
#include <limits>
#include <fstream>
using namespace chimbuko;

    
ADOutlier::AlgoParams::AlgoParams(): algorithm("hbos"), sstd_sigma(6.0), hbos_thres(0.99), glob_thres(true), hbos_max_bins(200){}  //, func_threshold_file("")

bool ADOutlier::AlgoParams::operator==(const AlgoParams &r) const{ return algorithm == r.algorithm && sstd_sigma == r.sstd_sigma && hbos_thres == r.hbos_thres && glob_thres == r.glob_thres && hbos_max_bins == r.hbos_max_bins; }

void ADOutlier::AlgoParams::setJson(const nlohmann::json &in){
#define JSON_CHECK(to) if(!in.contains(#to)) fatal_error("Expected key " #to);
#define JSON_GET(to) if(in.contains(#to)) to = in[#to].template get<decltype(to)>()
  //Check for required
  JSON_CHECK(algorithm);
  JSON_GET(algorithm);

  if(algorithm == "sstd"){
    JSON_CHECK(sstd_sigma);
  }else if(algorithm == "hbos"){
    JSON_CHECK(glob_thres);
    JSON_CHECK(hbos_max_bins);
  }
  if(algorithm == "hbos" || algorithm == "copod"){
    JSON_CHECK(hbos_thres);
  }
  //Get all available
  JSON_GET(sstd_sigma);
  JSON_GET(glob_thres);
  JSON_GET(hbos_max_bins);
  JSON_GET(hbos_thres);
#undef JSON_CHECK
#undef JSON_GET

}

void ADOutlier::AlgoParams::loadJsonFile(const std::string &filename){
  std::ifstream f(filename);
  nlohmann::json j; f >> j;
  setJson(j);
}

nlohmann::json ADOutlier::AlgoParams::getJson() const{
  nlohmann::json out;
#define JSON_SET(key) out[#key] = key
  JSON_SET(algorithm);
  JSON_SET(sstd_sigma);
  JSON_SET(hbos_thres);
  JSON_SET(glob_thres);
  JSON_SET(hbos_max_bins);
  return out;
#undef JSON_SET
}

int ADOutlier::AlgoParams::cmdlineParser::parse(const std::string &arg, const char** vals, const int vals_size){
  if(arg == m_arg){
    if(vals_size < 1) return -1;

    try{
      member.loadJsonFile(vals[0]);
    }catch(const std::exception &exc){
      return -1;
    }
    return 1;
  }
  return -1;
}
void ADOutlier::AlgoParams::cmdlineParser::help(std::ostream &os) const{
  os << m_arg << " : " << m_help_str;
}



/* ---------------------------------------------------------------------------
 * Implementation of ADOutlier class
 * --------------------------------------------------------------------------- */
ADOutlier::ADOutlier(int rank):
  m_param(nullptr), m_local_param(nullptr), m_use_ps(false), m_perf(nullptr), m_sync_call_count(0), m_global_model_sync_freq(1), m_rank(rank)
{
}

ADOutlier::~ADOutlier() {
    if (m_param) delete m_param;
    if (m_local_param) delete m_local_param;
}


// template<typename ADOutlierType>
// void loadPerFunctionThresholds(ADOutlierType* into, const std::string &filename){
//   if(filename == "") return;
//   std::ifstream in(filename);
//   if(!in.good()) fatal_error("Could not open function threshold file: " + filename);
//   nlohmann::json j;
//   in >> j;
//   if(in.fail()) fatal_error("Error reading from function threshold file");    
//   if(!j.is_array()) fatal_error("Function threshold file should be a json array");
//   for(auto it = j.begin(); it != j.end(); it++){
//     if(!it->contains("fname") || !it->contains("threshold")) fatal_error("Function threshold file invalid format");
//     const std::string &fname = (*it)["fname"];
//     double thres = (*it)["threshold"];
//     progressStream << "Set per-function threshold: \"" << fname << "\" " << thres << std::endl;
//     into->overrideFuncThreshold(fname, thres);
//   }
// }


ADOutlier *ADOutlier::set_algorithm(int rank, const AlgoParams &params) {
  if (params.algorithm == "sstd" || params.algorithm == "SSTD") {
    return new ADOutlierSSTD(rank,params.sstd_sigma);
  }
  else if (params.algorithm == "hbos" || params.algorithm == "HBOS") {
    ADOutlierHBOS* alg = new ADOutlierHBOS(rank,params.hbos_thres, params.glob_thres, params.hbos_max_bins);
    //loadPerFunctionThresholds(alg,params.func_threshold_file);
    return alg;
  }
  else if (params.algorithm == "copod" || params.algorithm == "COPOD") {
    ADOutlierCOPOD* alg = new ADOutlierCOPOD(rank,params.hbos_thres);
    //loadPerFunctionThresholds(alg,params.func_threshold_file);
    return alg;   
  }
  else{
    fatal_error("Invalid algorithm: " + params.algorithm);
  }
}

void ADOutlier::linkNetworkClient(ADNetClient *client){
  m_net_client = client;
  m_use_ps = (m_net_client != nullptr && m_net_client->use_ps());
}

std::pair<size_t,size_t> ADOutlier::sync_param(ParamInterface* param)
{
  if (!m_use_ps) {
    verboseStream << "m_use_ps not USED!" << std::endl;
    m_param->update(param->serialize());
    return std::make_pair(0, 0);
  }
  else {
    Message msg;
    msg.set_info(m_net_client->get_client_rank(), m_net_client->get_server_rank(), MessageType::REQ_ADD, BuiltinMessageKind::PARAMETERS);
    msg.setContent(param->serialize());
    size_t sent_sz = msg.size();

    m_net_client->send_and_receive(msg, msg);
    size_t recv_sz = msg.size();
    m_param->assign(msg.getContent());
    return std::make_pair(sent_sz, recv_sz);
  }
}


void ADOutlier::updateGlobalModel()
{
  PerfTimer timer;
  timer.start();
  
  if ( m_global_model_sync_freq > 0 && ( m_sync_call_count == 0 || (m_sync_call_count + m_rank) %  m_global_model_sync_freq == 0 ) ){ //apart from on first step, stagger updates over ranks by rank index
    verboseStream << "ADOutlier rank " << m_rank << " performing synchronization of local and global model on call count " << m_sync_call_count << std::endl;
    PerfTimer utimer;
    utimer.start();

    auto msgsz = sync_param(m_local_param);
    m_local_param->clear(); //flush local params    

    if(m_perf != nullptr){
      m_perf->add("param_update_ms", utimer.elapsed_ms());
      m_perf->add("param_sent_MB", (double)msgsz.first / 1000000.0); // MB
      m_perf->add("param_recv_MB", (double)msgsz.second / 1000000.0); // MB
    }
  }else{
    verboseStream << "ADOutlier NOT synchronizing global model on this step" << std::endl;
  }
  
  ++m_sync_call_count;

  if(m_perf != nullptr) m_perf->add("update_global_model_call_ms", timer.elapsed_ms());
}

void ADOutlier::setGlobalParameters(const std::string &to){
  m_param->clear();
  m_param->assign(to);
}


/* ---------------------------------------------------------------------------
 * Implementation of ADOutlierSSTD class
 * --------------------------------------------------------------------------- */
ADOutlierSSTD::ADOutlierSSTD(int rank, double sigma) : ADOutlier(rank), m_sigma(sigma) {
  m_param = new SstdParam();
  m_local_param = new SstdParam();
}

ADOutlierSSTD::~ADOutlierSSTD() {
}

void ADOutlierSSTD::run(ADDataInterface &data, int step) {

  //If using CUDA without precompiled kernels the first time a function is encountered takes much longer as it does a JIT compile
  //Python scripts also appear to take longer executing a function the first time
  //This is worked around by ignoring the first time a function is encountered (per device)
  //Set this environment variable to disable the workaround
  // bool cuda_jit_workaround = true;
  // if(const char* env_p = std::getenv("CHIMBUKO_DISABLE_CUDA_JIT_WORKAROUND")){
  //   cuda_jit_workaround = false;
  // }
  // for (auto it : *m_execDataMap) { //loop over functions (key is function index)
  //   unsigned long func_id = it.first;
  //   for (auto itt : it.second) { //loop over events for that function
  //     if(itt->get_label() == 0){ //has not been analyzed previously
  // 	//Update local counts of number of times encountered
  // 	std::array<unsigned long, 4> fkey({itt->get_pid(), itt->get_rid(), itt->get_tid(), func_id});
  // 	auto encounter_it = m_local_func_exec_count.find(fkey);
  // 	if(encounter_it == m_local_func_exec_count.end())
  // 	  encounter_it = m_local_func_exec_count.insert({fkey, 0}).first;
  // 	else
  // 	  encounter_it->second++;

  // 	if(!cuda_jit_workaround || encounter_it->second > 0){ //ignore first encounter to avoid including CUDA JIT compiles in stats (later this should be done only for GPU kernels
  // 	  param[func_id].push( this->getStatisticValue(*itt) );
  // 	}
  //     }
  //   }
  // }


  size_t ndset = data.nDataSets();
  std::vector< std::vector<ADDataInterface::Elem> > data_vals(ndset);
  SstdParam param;

  //Generate the statistics based on this IO step and also store data for use below to avoid having to call getDataSet more than once
  for(size_t dset_idx=0; dset_idx < ndset; dset_idx++){
    data_vals[dset_idx] = data.getDataSet(dset_idx);
    auto &dset_params = param[data.getDataSetModelIndex(dset_idx)];
    for(auto const &e : data_vals[dset_idx])
      dset_params.push(e.value);
  }
  if(enableVerboseLogging()){
    verboseStream << "ADOutlierSSTD::run obtained data for " << ndset << " data sets, sizes:";
    for(int i=0;i<ndset;i++) std::cout << " " << data_vals[i].size();
    std::cout << std::endl;
  }
  SstdParam &local_param = *(SstdParam*)m_local_param;
  local_param.update(param); 

  //Update temp runstats to include information collected previously (synchronizes with the parameter server if connected)
  updateGlobalModel();

  //Run anomaly detection algorithm
  for(size_t dset_idx=0; dset_idx < ndset; dset_idx++){
    labelData(data_vals[dset_idx], dset_idx, data.getDataSetModelIndex(dset_idx));
    data.recordDataSetLabels(data_vals[dset_idx], dset_idx);
  }
}

double ADOutlierSSTD::computeScore(double value, size_t model_idx, const SstdParam &stats) const{
  auto it = stats.get_runstats().find(model_idx);
  if(it == stats.get_runstats().end()) fatal_error("Model index not in stats!");
  double mean = it->second.mean();
  double std_dev = it->second.stddev();
  if(std_dev == 0.) std_dev = 1e-10; //distribution throws an error if std.dev = 0

  //boost::math::normal_distribution<double> dist(mean, std_dev);
  //double cdf_val = boost::math::cdf(dist, runtime); // P( X <= x ) for random variable X
  //double score = std::min(cdf_val, 1-cdf_val); //two-tailed

  //Using the CDF gives scores ~0 for basically any global outlier
  //Instead we will use the difference in runtime compared to the avg in units of the standard deviation
  double score = fabs( value - mean ) / std_dev;
  return score;
}


void ADOutlierSSTD::labelData(std::vector<ADDataInterface::Elem> &data_vals, size_t dset_idx, size_t model_idx){

  verboseStream << "Finding outliers in events for data set " << dset_idx << " of size " << data_vals.size() << std::endl;

  if(data_vals.size() == 0) return;

  SstdParam& param = *(SstdParam*)m_param;
  auto & fparam = param[model_idx];

  if (fparam.count() < 2){
    verboseStream << "Less than 2 events in stats associated with data set, stats not complete" << std::endl;
    for(auto &e : data_vals){ //still need to label all events
      e.label = ADDataInterface::EventType::Normal;
      e.score = 0;
    }
    return;
  }

  const double mean = fparam.mean();
  const double std = fparam.stddev();

  const double thr_hi = mean + m_sigma * std;
  const double thr_lo = mean - m_sigma * std;

  for(auto &e : data_vals){
    e.label = (thr_lo > e.value || thr_hi < e.value) ? ADDataInterface::EventType::Outlier : ADDataInterface::EventType::Normal;
    e.score = computeScore(e.value, model_idx, param);
  }
}




/* ---------------------------------------------------------------------------
 * Implementation of ADOutlierHBOS class
 * --------------------------------------------------------------------------- */

ADOutlierHBOS::ADOutlierHBOS(int rank, double threshold, bool use_global_threshold, int maxbins) : ADOutlier(rank), m_alpha(78.88e-32), m_threshold(threshold), m_use_global_threshold(use_global_threshold), m_maxbins(maxbins) {
  m_param = new HbosParam();
  m_local_param = new HbosParam();
}

ADOutlierHBOS::~ADOutlierHBOS() {
}

// double ADOutlierHBOS::getFunctionThreshold(const std::string &fname) const{
//   double hbos_threshold = m_threshold; //default threshold
//   //check to see if we have an override
//   auto it = m_func_threshold_override.find(fname);
//   if(it != m_func_threshold_override.end())
//     hbos_threshold = it->second;
//   return hbos_threshold;
// }


void ADOutlierHBOS::run(ADDataInterface &data, int step) {
  size_t ndset = data.nDataSets();
  std::vector< std::vector<ADDataInterface::Elem> > data_vals(ndset);

  //Generate the statistics based on this IO step
  const HbosParam& global_param = *(HbosParam const*)m_param;
  HbosParam param;
  param.setMaxBins(m_maxbins); //communicates the maxbins parameter to the pserver (it should be the same for all clients)
  
  for(size_t dset_idx=0; dset_idx < ndset; dset_idx++){
    data_vals[dset_idx] = data.getDataSet(dset_idx);

    std::vector<double> values(data_vals[dset_idx].size());
    size_t i=0;
    for(auto const &e : data_vals[dset_idx])
      values[i++] = e.value;

    verboseStream << "Data set " << dset_idx << " has " << values.size() << " unlabeled data points" << std::endl;

    param.generate_histogram(data.getDataSetModelIndex(dset_idx), values, 0, &global_param); //initialize global threshold to 0 so that it is overridden by the merge
  }
  HbosParam &local_param = *(HbosParam*)m_local_param;
  local_param.update(param); 

  //Update temp runstats to include information collected previously (synchronizes with the parameter server if connected)
  updateGlobalModel();

  //Run anomaly detection algorithm
  for(size_t dset_idx=0; dset_idx < ndset; dset_idx++){    
    labelData(data_vals[dset_idx], dset_idx, data.getDataSetModelIndex(dset_idx));
    data.recordDataSetLabels(data_vals[dset_idx], dset_idx);   
  }
}


void ADOutlierHBOS::labelData(std::vector<ADDataInterface::Elem> &data_vals, size_t dset_idx, size_t model_idx){
  verboseStream << "Finding outliers in events for data set " << dset_idx << " of size " << data_vals.size() << std::endl;

  if(data_vals.size() == 0) return;

  HbosParam& param = *(HbosParam*)m_param;
  HbosFuncParam &fparam = param[model_idx];
  const Histogram &hist = fparam.getHistogram();

  auto const & bin_counts = hist.counts();
  size_t nbin = bin_counts.size();

  verboseStream << "Number of bins " << nbin << std::endl;

  //Check that the histogram contains bins
  if(nbin == 0){
    //As the pserver global model update is delayed, initially the clients may receive an empty model from the pserver for this data set
    //Given that the model at this stage is unreliable anyway, we simply skip the set and label the data as normal
    verboseStream << "Global model is empty, skipping outlier evaluation for data set " << dset_idx << std::endl;
    for(auto &e : data_vals) e.label = ADDataInterface::EventType::Normal;
    return;
  }

  //Bounds of the range of possible scores
  const double max_possible_score = -1 * log2(0.0 + m_alpha); //-log2(78.88e-32) ~ 100.0 by default (i.e. the theoretical max score)

  //Find the smallest and largest scores in the histogram (excluding empty bins)
  double min_score = std::numeric_limits<double>::max();
  double max_score = std::numeric_limits<double>::lowest();  

  //Compute scores
  Histogram::CountType tot_runtimes = std::accumulate(bin_counts.begin(), bin_counts.end(), Histogram::CountType(0));

  std::vector<double> out_scores_i(nbin);

  verboseStream << "out_scores_i: " << std::endl;
  for(int i=0; i < nbin; i++){
    Histogram::CountType count = bin_counts[i];
    double prob = double(count)/ tot_runtimes;
    double score = -1 * log2(prob + m_alpha);
    out_scores_i[i] = score;
    verboseStream << "Bin " << i << ", Range " << hist.binEdgeLower(i) << "-" << hist.binEdgeUpper(i) << ", Count: " << count << ", Probability: " << prob << ", score: "<< score << std::endl;
    if(prob > 0 ) {
      min_score = std::min(min_score,score);
      max_score = std::max(max_score,score);
    }
  }
  verboseStream << std::endl;
  verboseStream << "min_score = " << min_score << std::endl;
  verboseStream << "max_score = " << max_score << std::endl;

  //Get the threshold appropriate to the data set
  double hbos_threshold = m_threshold;  //getFunctionThreshold(fname);

#if 1
  //Compute threshold as a fraction of the range of scores in the histogram

  //Convert threshold into a score threshold
  double l_threshold = min_score + (hbos_threshold * (max_score - min_score));

  /*
    l_threshold = min_score*(1-m_threshold) + m_threshold*max_score
    min_score = -log2(p_max + m_alpha)
    max_score = -log2(p_min + m_alpha)
    l_threshold = -log2( [p_max + m_alpha]^[1-m_threshold] ) -log2( [p_min + m_alpha]^m_threshold )
    l_threshold = log2( [p_max + m_alpha]^[m_threshold-1] ) +log2( [p_min + m_alpha]^-m_threshold )
                = log2( [p_max + m_alpha]^-[1-m_threshold]  *  [p_min + m_alpha]^-m_threshold )
  */

  verboseStream << "Local threshold " << l_threshold << std::endl;
  if(m_use_global_threshold) {
    double g_threshold  = fparam.getInternalGlobalThreshold();
    verboseStream << "Global threshold before comparison with local threshold =  " << g_threshold << std::endl;

    if(l_threshold < g_threshold) {
      verboseStream << "Using global threshold as it is more stringent" << std::endl;
      l_threshold = g_threshold;
    } else {
      verboseStream << "Using local threshold as it is more stringent" << std::endl;
      fparam.setInternalGlobalThreshold(l_threshold);  //update the global score threshold to the new, more stringent test
    }
  }
#else
  //Rather than using the score range, which can become large for bins with small counts and is sensitive to the bin size and to bin shaving,
  //we can define the threshold based on the relative probability of a bin to the peak bin
  //p_i / p_max <= Q
  //Q = 1- hbos_threshold   as hbos_threshold default 0.99

  //s=-log2(p + alpha)

  //Threshold at 
  //p_i = Q p_max
  //2^-s_i -alpha = Q ( 2^-s_min - alpha )
  //s_i = -log2 [ Q 2^-s_min + (1-Q)alpha ]
  if(hbos_threshold >= 1.0) fatal_error("Invalid threshold value");
  double Q = 1-hbos_threshold;
  double l_threshold = -log2( Q*pow(2,-min_score) + (1.-Q)*m_alpha );
  
  verboseStream << "Condition p_i/p_max <= " << Q << " maps to local threshold " << l_threshold << std::endl;
#endif

  //Compute HBOS based score for each datapoint
  const double bin_width = hist.binWidth();
  verboseStream << "Bin width: " << bin_width << std::endl;

  int top_out = 0;
  for(auto &v : data_vals){
    const double val_i = v.value;
    double &ad_score = v.score;
    const int bin_ind = hist.getBin(val_i, 0.05); //allow events within 5% of the bin width away from the histogram edges to be included in the first/last bin
    verboseStream << "bin_ind: " << bin_ind << " for runtime_i: " << val_i << ", where num_bins: "<< nbin << std::endl;
    
    if( bin_ind == Histogram::LeftOfHistogram || bin_ind == Histogram::RightOfHistogram){
      //Sample (datapoint) lies outside of the histogram
      verboseStream << val_i << " is on " << (bin_ind == Histogram::LeftOfHistogram ? "left" : "right")  << " of histogram and an outlier" << std::endl;
      ad_score = max_possible_score;
      verboseStream << "ad_score(max_possible_score): " << ad_score << std::endl;
    }else{
      //Sample is within the histogram
      verboseStream << val_i << " maybe be an outlier" << std::endl;
      ad_score = out_scores_i[bin_ind];
      verboseStream << "ad_score(else): " << ad_score << ", bin_ind: " << bin_ind  << ", num_bins: " << nbin << ", out_scores_i size: " << out_scores_i.size() << std::endl;
    }

    //handle when ad_score = 0
    //This is valid when there is only one bin as the probability is 1 and log(1) = 0
    //Note that the total number of bins can be > 1 providing the other bins have 0 counts
    if (ad_score <= 0 ){
      int nbin_nonzero = 0;
      for(unsigned int c : hist.counts())
	if(c>0) ++nbin_nonzero;
      if(nbin_nonzero != 1){
	double prob;
	if(bin_ind == Histogram::LeftOfHistogram || bin_ind == Histogram::RightOfHistogram) prob = 1.0;
	else prob = double(bin_counts[bin_ind])/tot_runtimes;
	std::stringstream ss; ss << "ad_score " << ad_score << " <= 0 but #bins with non zero count, " << nbin_nonzero << " is not 1. Data set " << dset_idx << ", value " << val_i <<  
				 ", prob " << prob << ", bin index " << bin_ind << " of hist with bounds " << hist.printBounds();
	recoverable_error(ss.str());
      }
    }

    verboseStream << "ad_score: " << ad_score << ", l_threshold: " << l_threshold << std::endl;

    //Compare the ad_score with the threshold
    if (ad_score >= l_threshold) {
      v.label = ADDataInterface::EventType::Outlier;
      verboseStream << "!!!!!!!Detected outlier on data set " << dset_idx << " value " << val_i << " score " << ad_score << " (threshold " << l_threshold << ")" << std::endl;
    }else {
      v.label = ADDataInterface::EventType::Normal;
      verboseStream << "Detected normal event on data set " << dset_idx << " value " << val_i << " score " << ad_score << " (threshold " << l_threshold << ")" << std::endl;
    }
  } //loop over data points
}


/* ---------------------------------------------------------------------------
 * Implementation of ADOutlierCOPOD class
 * --------------------------------------------------------------------------- */
ADOutlierCOPOD::ADOutlierCOPOD(int rank, double threshold, bool use_global_threshold) : ADOutlier(rank), m_alpha(78.88e-32), m_threshold(threshold), m_use_global_threshold(use_global_threshold) {
  m_param = new CopodParam();
  m_local_param = new CopodParam();
}

ADOutlierCOPOD::~ADOutlierCOPOD() {}

// double ADOutlierCOPOD::getFunctionThreshold(const std::string &fname) const{
//   double copod_threshold = m_threshold; //default threshold
//   //check to see if we have an override
//   auto it = m_func_threshold_override.find(fname);
//   if(it != m_func_threshold_override.end())
//     copod_threshold = it->second;
//   return copod_threshold;
// }


void ADOutlierCOPOD::run(ADDataInterface &data, int step) {
  size_t ndset = data.nDataSets();
  std::vector< std::vector<ADDataInterface::Elem> > data_vals(ndset);

  //Generate the statistics based on this IO step
  const CopodParam& global_param = *(CopodParam const*)m_param;
  CopodParam param;
  
  for(size_t dset_idx=0; dset_idx < ndset; dset_idx++){
    data_vals[dset_idx] = data.getDataSet(dset_idx);

    std::vector<double> values(data_vals[dset_idx].size());
    size_t i=0;
    for(auto const &e : data_vals[dset_idx])
      values[i++] = e.value;

    verboseStream << "Data set " << dset_idx << " has " << values.size() << " unlabeled data points" << std::endl;

    CopodFuncParam &fparam = param[data.getDataSetModelIndex(dset_idx)];
    Histogram &hist = fparam.getHistogram();
    if (values.size() > 0) {
      verboseStream << "Creating local histogram for data set " << dset_idx << " for " << values.size() << " data points" << std::endl;
      hist.create_histogram(values);
    }
  }
  CopodParam &local_param = *(CopodParam*)m_local_param;
  local_param.update(param); 

  //Update temp runstats to include information collected previously (synchronizes with the parameter server if connected)
  updateGlobalModel();

  //Run anomaly detection algorithm
  for(size_t dset_idx=0; dset_idx < ndset; dset_idx++){    
    labelData(data_vals[dset_idx], dset_idx, data.getDataSetModelIndex(dset_idx));
    data.recordDataSetLabels(data_vals[dset_idx], dset_idx);   
  }
}

//Compute the COPOD score
  //We take the score as the largest of:
  // 1) the avg of the right and left tailed 
  // 2) the skewness corrected ECDF value
inline double copod_score(const double value_i, const Histogram &hist, const Histogram &nhist, 
			  const double m_alpha, const int p_sign, const int n_sign,
			  Histogram::empiricalCDFworkspace &w, Histogram::empiricalCDFworkspace &nw){
  double left_tailed_prob = hist.empiricalCDF(value_i, &w);
  double right_tailed_prob = nhist.empiricalCDF(-value_i, &nw);

  //When generating the histogram we place the lower bound just before the minimum value
  //This means the CDF for the minimum value is exactly 0 whereas it should be at least 1/N
  //This introduces an error in COPOD; whenever a new minimum is encountered, it is marked as an outlier even if it should not be
  //To correct for this, if a data point is larger than min the CDF is shifted by 1/N
  verboseStream << "Value " << value_i << " hist min " << hist.getMin() << std::endl;
  if(value_i >= hist.getMin() ){ 
    verboseStream << "Shifting left-tailed prob from " << left_tailed_prob << " to ";
    left_tailed_prob += 1./w.getSum(hist); 
    left_tailed_prob = std::min(1.0, left_tailed_prob); 
    verboseStreamAdd << left_tailed_prob << std::endl;
  }
  verboseStream << "Negated value " << -value_i << " nhist min " << nhist.getMin() << std::endl;
  if(-value_i >= nhist.getMin() ){ 
    verboseStream << "Shifting right-tailed prob from " << right_tailed_prob << " to ";
    right_tailed_prob += 1./nw.getSum(nhist); 
    right_tailed_prob = std::min(1.0, right_tailed_prob); 
    verboseStreamAdd << right_tailed_prob << std::endl;
  }

  double left_tailed_score = -log2(left_tailed_prob + m_alpha);
  double right_tailed_score = -log2(right_tailed_prob + m_alpha);
  double avg_lr_score = (left_tailed_score + right_tailed_score)/2.;
  double corrected_score = (left_tailed_score * -1 * p_sign) + (right_tailed_score * n_sign);
  
  double ad_score = std::max(avg_lr_score, corrected_score);
  verboseStream << "Value: " << value_i 
		<< " left-tailed prob: " << left_tailed_prob << " left-tailed score: " << left_tailed_score 
		<< " right-tailed prob: " << right_tailed_prob << " right-tailed score: " << right_tailed_score 
		<< " avg l/r score: " << avg_lr_score << " skewness corrected score: " << corrected_score 
		<< " final score: " << ad_score << std::endl;
  return ad_score;
}


void ADOutlierCOPOD::labelData(std::vector<ADDataInterface::Elem> &data_vals, size_t dset_idx, size_t model_idx){
  verboseStream << "Finding outliers in events for data set " << dset_idx << std::endl;
  verboseStream << "data Size: " << data_vals.size() << std::endl;

  if(data_vals.size() == 0) return;

  CopodParam& param = *(CopodParam*)m_param;
  CopodFuncParam &fparam = param[model_idx];
  Histogram &hist = fparam.getHistogram();

  auto const & bin_counts = hist.counts();

  size_t nbin = bin_counts.size();

  verboseStream << "Number of bins " << nbin << std::endl;

  verboseStream << "Histogram:" << std::endl << hist << std::endl;

  //Check that the histogram contains bins
  if(nbin == 0){
    //As the pserver global model update is delayed, initially the clients may receive an empty model from the pserver for this function
    //Given that the model at this stage is unreliable anyway, we simply skip the function and label the data as normal
    verboseStream << "Global model is empty, skipping outlier evaluation for data set " << dset_idx << std::endl;
    for(auto &e : data_vals) e.label = ADDataInterface::EventType::Normal;
    return;
  }

  //Compute the skewness of the histogram
  double skewness = hist.skewness();
  const int p_sign = (skewness - 1) < 0 ? -1 : (skewness - 1) > 0 ? 1 : 0; //sign(skewness-1)
  const int n_sign = (skewness + 1) < 0 ? -1 : (skewness + 1) > 0 ? 1 : 0; //sign(skewness+1)

  //Negate the histogram to compute right-tailed ECDFs
  Histogram nhist = -hist;
  verboseStream << "Inverted Histogram: " << std::endl << nhist << std::endl;

  //Determine the outlier threshold by computing the range of scores for data points *in the histogram*
  verboseStream << "Computing score range from histogram" << std::endl;
  Histogram::empiricalCDFworkspace w, nw;
  double min_score = -1 * log2(0.0 + m_alpha);   //Compute the range of scores in order to determine the outlier threshold
  double max_score = log2(1.0 + m_alpha) - min_score;
  
  for(int b=0;b<nbin;b++){
    double v = hist.binValue(b);
    double ad_score = copod_score(v, hist, nhist, m_alpha, p_sign, n_sign, w, nw);

    min_score = std::min(ad_score, min_score);
    max_score = std::max(ad_score, max_score);
  }
  verboseStream << "min_score = " << min_score << std::endl;
  verboseStream << "max_score = " << max_score << std::endl;

  //Compute threshold
  //Get the threshold appropriate to the function
  double func_threshold = m_threshold; //getFunctionThreshold(fname); 

  double l_threshold = (max_score < 0) ? (-1 * func_threshold * (max_score - min_score)) : min_score + (func_threshold * (max_score - min_score));
  verboseStream << "l_threshold computed: " << l_threshold << std::endl;
  if(m_use_global_threshold) {
    double g_threshold = fparam.getInternalGlobalThreshold();
    verboseStream << "Global threshold before comparison with local threshold =  " << g_threshold << std::endl;
    if(l_threshold < g_threshold && g_threshold > (-1 * log2(1.00001))) {
      l_threshold = g_threshold;
    } else {
      fparam.setInternalGlobalThreshold(l_threshold);
    }
  }

  verboseStream << "Performing outlier detection" << std::endl;
  //Perform outlier detection  
  unsigned long n_outliers = 0;
  for (auto &e : data_vals) {
    e.score = copod_score(e.value, hist, nhist, m_alpha, p_sign, n_sign, w, nw);
      
    verboseStream << "value: " << e.value << " score: " << e.score << ", l_threshold: " << l_threshold << std::endl;

    if (e.score >= l_threshold) {
      e.label = ADDataInterface::EventType::Outlier;
      verboseStream << "!!!!!!!Detected outlier on data set " << dset_idx << " value " << e.value << " score " << e.score << " (threshold " << l_threshold << ")" << std::endl;
    }else {
      e.label = ADDataInterface::EventType::Normal;
      verboseStream << "Detected normal event on data set " << dset_idx << " value " << e.value << " score " << e.score << " (threshold " << l_threshold << ")" << std::endl;
    }
  }//data loop
}