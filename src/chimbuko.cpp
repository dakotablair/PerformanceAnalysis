#include <limits>
#include "chimbuko/chimbuko.hpp"
#include "chimbuko/verbose.hpp"
#include "chimbuko/util/error.hpp"
#include "chimbuko/util/time.hpp"
#include "chimbuko/util/memutils.hpp"

using namespace chimbuko;

ChimbukoParams::ChimbukoParams(): rank(-1234),  //not set!
				  program_idx(0),
				  verbose(true),
				  outlier_sigma(6.),
				  trace_engineType("BPFile"), trace_data_dir("."), trace_inputFile("TAU_FILENAME-BINARYNAME"),
				  trace_connect_timeout(60),
                                  net_recv_timeout(30000),
				  pserver_addr(""), hpserver_nthr(1),
				  anom_win_size(10),
				  ad_algorithm("hbos"),
				  hbos_threshold(0.99),
				  hbos_use_global_threshold(true),
				  hbos_max_bins(200),
#ifdef ENABLE_PROVDB
				  provdb_addr_dir(""), nprovdb_shards(1), nprovdb_instances(1), provdb_mercury_auth_key(""),
#endif
				  prov_outputpath(""),
                                  prov_record_startstep(-1),
                                  prov_record_stopstep(-1),  
				  perf_outputpath(""), perf_step(10),
				  only_one_frame(false), interval_msec(0),
				  err_outputpath(""), parser_beginstep_timeout(30), override_rank(false),
                                  outlier_statistic("exclusive_runtime"),
                                  step_report_freq(1),
                                  analysis_step_freq(1),
                                  read_ignored_corrid_funcs(""),
                                  max_frames(-1),
                                  func_threshold_file(""),
                                  ignored_func_file(""),
                                  monitoring_watchlist_file(""),
                                  monitoring_counter_prefix(""),
                                  prov_min_anom_time(0)
{}

void ChimbukoParams::print() const{
  std::cout << "AD Algorithm: " << ad_algorithm
	    << "\nProgram Idx: " << program_idx
	    << "\nRank       : " << rank
	    << "\nEngine     : " << trace_engineType
	    << "\nBP in dir  : " << trace_data_dir
	    << "\nBP file    : " << trace_inputFile
#ifdef _USE_ZMQNET
	    << "\nPS Addr    : " << pserver_addr
#endif
	    << "\nSigma      : " << outlier_sigma
			<< "\nHBOS/COPOD Threshold: " << hbos_threshold
			<< "\nUsing Global threshold: " << hbos_use_global_threshold
	    << "\nWindow size: " << anom_win_size

	    << "\nInterval   : " << interval_msec << " msec"
            << "\nNetClient Receive Timeout : " << net_recv_timeout << "msec"
	    << "\nPerf. metric outpath : " << perf_outputpath
	    << "\nPerf. step   : " << perf_step
            << "\nAnalysis step freq. : " << analysis_step_freq ;
#ifdef ENABLE_PROVDB
  if(provdb_addr_dir.size()){
    std::cout << "\nProvDB addr dir: " << provdb_addr_dir
	      << "\nProvDB shards: " << nprovdb_shards
      	      << "\nProvDB server instances: " << nprovdb_instances;
  }
#endif
  if(prov_outputpath.size())
    std::cout << "\nProvenance outpath : " << prov_outputpath;

  std::cout << std::endl;
}


Chimbuko::Chimbuko(): m_parser(nullptr), m_event(nullptr), m_outlier(nullptr), m_io(nullptr), m_net_client(nullptr),
#ifdef ENABLE_PROVDB
		      m_provdb_client(nullptr),
#endif
		      m_anomaly_provenance(nullptr),
		      m_metadata_parser(nullptr),
		      m_monitoring(nullptr),
		      m_is_initialized(false){}

Chimbuko::~Chimbuko(){
  finalize();
}

void Chimbuko::initialize(const ChimbukoParams &params){
  PerfTimer total_timer, timer;

  if(m_is_initialized) finalize();
  m_params = params;

  //Check parameters
  if(m_params.rank < 0) throw std::runtime_error("Rank not set or invalid");

#ifdef ENABLE_PROVDB
  if(m_params.provdb_addr_dir.size() == 0 && m_params.prov_outputpath.size() == 0)
    throw std::runtime_error("Neither provenance database address or provenance output dir are set - no provenance data will be written!");
#else
  if(m_params.prov_outputpath.size() == 0)
    throw std::runtime_error("Provenance output dir is not set - no provenance data will be written!");
#endif

  //Reset state
  m_execdata_first_event_ts = m_execdata_last_event_ts = 0;
  m_execdata_first_event_ts_set = false;

  m_n_func_events_accum_prd = m_n_comm_events_accum_prd = m_n_counter_events_accum_prd = 0;
  m_n_outliers_accum_prd = 0; /**< Total number of outiers detected since last write of periodic data*/
  m_n_steps_accum_prd = 0; /**< Number of steps since last write of periodic data */

  //Setup perf output
  std::string ad_perf = stringize("ad_perf.%d.%d.json", m_params.program_idx, m_params.rank);
  m_perf.setWriteLocation(m_params.perf_outputpath, ad_perf);

  std::string ad_perf_prd = stringize("ad_perf_prd.%d.%d.log", m_params.program_idx, m_params.rank);
  m_perf_prd.setWriteLocation(m_params.perf_outputpath, ad_perf_prd);

  //Initialize error collection
  if(params.err_outputpath.size())
    set_error_output_file(m_params.rank, stringize("%s/ad_error.%d", params.err_outputpath.c_str(), m_params.program_idx));
  else
    set_error_output_stream(m_params.rank, &std::cerr);

  //Connect to the provenance database and/or initialize provenance IO
#ifdef ENABLE_PROVDB
  timer.start();
  init_provdb();
  m_perf.add("ad_initialize_provdb_ms", timer.elapsed_ms());
#endif
  init_io(); //will write provenance info if provDB not in use

  //Connect to the parameter server
  timer.start();
  init_net_client();
  m_perf.add("ad_initialize_net_client_ms", timer.elapsed_ms());

  //Connect to TAU; process will be blocked at this line until it finds writer (in SST mode)
  timer.start();
  init_parser();
  m_perf.add("ad_initialize_parser_ms", timer.elapsed_ms());

  //Event and outlier objects must be initialized in order after parser
  init_metadata_parser();
  init_event(); //requires parser and metadata parser
  init_outlier(); //requires event
  init_counter(); //requires parser
  init_monitoring(); //requires parser
  init_provenance_gatherer(); //requires most components

  m_is_initialized = true;

  headProgressStream(m_params.rank) << "driver rank " << m_params.rank << " has initialized successfully" << std::endl;
  m_perf.add("ad_initialize_total_ms", total_timer.elapsed_ms());
}


void Chimbuko::init_io(){
  m_io = new ADio(m_params.program_idx, m_params.rank);
  m_io->setDispatcher();
  m_io->setDestructorThreadWaitTime(0); //don't know why we would need a wait

  if(m_params.prov_outputpath.size())
    m_io->setOutputPath(m_params.prov_outputpath);
  m_ptr_registry.registerPointer(m_io);
}

void Chimbuko::init_parser(){
  headProgressStream(m_params.rank) << "driver rank " << m_params.rank << " connecting to application trace stream" << std::endl;
  if(m_params.pserver_addr.length() > 0 && !m_net_client) throw std::runtime_error("Net client must be initialized before calling init_parser");
  m_parser = new ADParser(m_params.trace_data_dir + "/" + m_params.trace_inputFile, m_params.program_idx, m_params.rank, m_params.trace_engineType,
			  m_params.trace_connect_timeout);
  m_parser->linkPerf(&m_perf);
  m_parser->setBeginStepTimeout(m_params.parser_beginstep_timeout);
  m_parser->setDataRankOverride(m_params.override_rank);
  m_parser->linkNetClient(m_net_client); //allow the parser to talk to the pserver to obtain global function indices
  m_ptr_registry.registerPointer(m_parser);
  headProgressStream(m_params.rank) << "driver rank " << m_params.rank << " successfully connected to application trace stream" << std::endl;
}

void Chimbuko::init_event(){
  if(!m_parser) fatal_error("Parser must be initialized before calling init_event");
  if(!m_metadata_parser) fatal_error("Metadata parser must be initialized before calling init_event");
  m_event = new ADEvent(m_params.verbose);
  m_event->linkFuncMap(m_parser->getFuncMap());
  m_event->linkEventType(m_parser->getEventType());
  m_event->linkCounterMap(m_parser->getCounterMap());
  m_event->linkGPUthreadMap(&m_metadata_parser->getGPUthreadMap());
  m_ptr_registry.registerPointer(m_event);

  //Setup ignored correlation IDs for given functions
  if(m_params.read_ignored_corrid_funcs.size()){
    std::ifstream in(m_params.read_ignored_corrid_funcs);
    if(in.is_open()) {
      std::string func;
      while (std::getline(in, func)){
	headProgressStream(m_params.rank) << "Ignoring correlation IDs for function \"" << func << "\"" << std::endl;
	m_event->ignoreCorrelationIDsForFunction(func);
      }
      in.close();
    }else{
      fatal_error("Failed to open ignored-corried-func file " + m_params.read_ignored_corrid_funcs);
    }

  }
}

void Chimbuko::init_net_client(){
  if(m_params.pserver_addr.length() > 0){
    headProgressStream(m_params.rank) << "driver rank " << m_params.rank << " connecting to parameter server" << std::endl;

    //If using the hierarchical PS we need to choose the appropriate port to connect to as an offset of the base port
    if(m_params.hpserver_nthr <= 0) throw std::runtime_error("Chimbuko::init_net_client Input parameter hpserver_nthr cannot be <1");
    else if(m_params.hpserver_nthr > 1){
      std::string orig = m_params.pserver_addr;
      m_params.pserver_addr = getHPserverIP(m_params.pserver_addr, m_params.hpserver_nthr, m_params.rank);
      verboseStream << "AD rank " << m_params.rank << " connecting to endpoint " << m_params.pserver_addr << " (base " << orig << ")" << std::endl;
    }

    m_net_client = new ADThreadNetClient;
    m_net_client->linkPerf(&m_perf);
    m_net_client->setRecvTimeout(m_params.net_recv_timeout);
    m_net_client->connect_ps(m_params.rank, 0, m_params.pserver_addr);
    if(!m_net_client->use_ps()) fatal_error("Could not connect to parameter server");
    m_ptr_registry.registerPointer(m_net_client);

    headProgressStream(m_params.rank) << "driver rank " << m_params.rank << " successfully connected to parameter server" << std::endl;
  }
}


void Chimbuko::init_outlier(){
  if(!m_event) throw std::runtime_error("Event managed must be initialized before calling init_outlier");

  ADOutlier::AlgoParams params;
  params.hbos_thres = m_params.hbos_threshold;
  params.glob_thres = m_params.hbos_use_global_threshold;
  params.sstd_sigma = m_params.outlier_sigma;
  params.hbos_max_bins = m_params.hbos_max_bins;
  params.func_threshold_file = m_params.func_threshold_file;

  ADOutlier::OutlierStatistic stat;
  if(m_params.outlier_statistic == "exclusive_runtime") params.stat = ADOutlier::ExclusiveRuntime;
  else if(m_params.outlier_statistic == "inclusive_runtime") params.stat = ADOutlier::InclusiveRuntime;
  else{ fatal_error("Invalid statistic"); }

  m_outlier = ADOutlier::set_algorithm(m_params.ad_algorithm, params);
  m_outlier->linkExecDataMap(m_event->getExecDataMap()); //link the map of function index to completed calls such that they can be tagged as outliers if appropriate
  if(m_net_client) m_outlier->linkNetworkClient(m_net_client);
  m_outlier->linkPerf(&m_perf);
  m_ptr_registry.registerPointer(m_outlier);

  //Read ignored functions
  if(m_params.ignored_func_file.size()){
    std::ifstream in(m_params.ignored_func_file);
    if(in.is_open()) {
      std::string func;
      while (std::getline(in, func)){
	headProgressStream(m_params.rank) << "Skipping anomaly detection for function \"" << func << "\"" << std::endl;
	m_outlier->setIgnoreFunction(func);
      }
      in.close();
    }else{
      fatal_error("Failed to open ignored-func file " + m_params.ignored_func_file);
    }
  }


}

void Chimbuko::init_counter(){
  if(!m_parser) throw std::runtime_error("Parser must be initialized before calling init_counter");
  m_counter = new ADCounter();
  m_counter->linkCounterMap(m_parser->getCounterMap());
  m_ptr_registry.registerPointer(m_counter);
}

#ifdef ENABLE_PROVDB
void Chimbuko::init_provdb(){
  if(m_params.provdb_mercury_auth_key != ""){
    headProgressStream(m_params.rank) << "driver rank " << m_params.rank << " setting Mercury authorization key to \"" << m_params.provdb_mercury_auth_key << "\"" << std::endl;
    ADProvenanceDBengine::setMercuryAuthorizationKey(m_params.provdb_mercury_auth_key);
  }
  
  m_provdb_client = new ADProvenanceDBclient(m_params.rank);
  if(m_params.provdb_addr_dir.length() > 0){
    headProgressStream(m_params.rank) << "driver rank " << m_params.rank << " connecting to provenance database" << std::endl;
    m_provdb_client->connectMultiServer(m_params.provdb_addr_dir, m_params.nprovdb_shards, m_params.nprovdb_instances);
    headProgressStream(m_params.rank) << "driver rank " << m_params.rank << " successfully connected to provenance database" << std::endl;
  }

  m_provdb_client->linkPerf(&m_perf);
  m_ptr_registry.registerPointer(m_provdb_client);
}
#endif

void Chimbuko::init_metadata_parser(){
  m_metadata_parser = new ADMetadataParser;
  m_ptr_registry.registerPointer(m_metadata_parser);
}

void Chimbuko::init_monitoring(){
  m_monitoring = new ADMonitoring;
  m_monitoring->linkCounterMap(m_parser->getCounterMap());
  if(m_params.monitoring_watchlist_file.size())
    m_monitoring->parseWatchListFile(m_params.monitoring_watchlist_file);
  else
    m_monitoring->setDefaultWatchList();
  if(m_params.monitoring_counter_prefix.size())
    m_monitoring->setCounterPrefix(m_params.monitoring_counter_prefix);
  m_ptr_registry.registerPointer(m_monitoring);
}

void Chimbuko::init_provenance_gatherer(){
  m_anomaly_provenance = new ADAnomalyProvenance(*m_event);
  m_anomaly_provenance->linkPerf(&m_perf);
  m_anomaly_provenance->linkAlgorithmParams(m_outlier->get_global_parameters());
  m_anomaly_provenance->linkMonitoring(m_monitoring);
  m_anomaly_provenance->linkMetadata(m_metadata_parser);
  m_anomaly_provenance->setWindowSize(m_params.anom_win_size);
  m_anomaly_provenance->setMinimumAnomalyTime(m_params.prov_min_anom_time);
  m_ptr_registry.registerPointer(m_anomaly_provenance);
}


void Chimbuko::finalize()
{
  PerfTimer timer;
  if(!m_is_initialized) return;

  //Always dump perf at end
  m_perf.write();

  //For debug purposes, write out any unmatched correlation ID events that we encountered as recoverable errors
  if(m_event->getUnmatchCorrelationIDevents().size() > 0){
    for(auto c: m_event->getUnmatchCorrelationIDevents()){
      std::stringstream ss;
      ss << "Unmatched correlation ID: " << c.first << " from event " << c.second->get_json().dump();
      recoverable_error(ss.str());
    }
  }

  headProgressStream(m_params.rank) << "driver rank " << m_params.rank << " run complete" << std::endl;

  if(m_net_client) m_net_client->disconnect_ps();

  m_ptr_registry.resetPointers(); //delete all pointers in reverse order to which they were instantiated
  m_ptr_registry.deregisterPointers(); //allow them to be re-registered if init is called again

  m_exec_ignore_counters.clear(); //reset the ignored counters list
  
  m_is_initialized = false;

  m_perf.add("ad_finalize_total_ms", timer.elapsed_ms());
  m_perf.write();
}

//Returns false if beginStep was not successful
bool Chimbuko::parseInputStep(int &step,
			      unsigned long long& n_func_events,
			      unsigned long long& n_comm_events,
			      unsigned long long& n_counter_events
			      ){
  if (!m_parser->getStatus()) return false;

  PerfTimer timer, total_timer;
  total_timer.start();

  int expect_step = step+1;

  //Decide whether to report step progress
  bool do_step_report = enableVerboseLogging() || (m_params.step_report_freq > 0 && expect_step % m_params.step_report_freq == 0);

  if(do_step_report){ headProgressStream(m_params.rank) << "driver rank " << m_params.rank << " commencing step " << expect_step << std::endl; }

  timer.start();
  m_parser->beginStep();
  if (!m_parser->getStatus()){
    verboseStream << "driver parser appears to have disconnected, ending" << std::endl;
    return false;
  }
  m_perf.add("ad_parse_input_begin_step_ms", timer.elapsed_ms());

  // get trace data via SST
  step = m_parser->getCurrentStep();
  if(step != expect_step){ recoverable_error(stringize("Got step %d expected %d\n", step, expect_step)); }

  if(do_step_report){ headProgressStream(m_params.rank) << "driver rank " << m_params.rank << " commencing input parse for step " << step << std::endl; }

  verboseStream << "driver rank " << m_params.rank << " updating attributes" << std::endl;
  timer.start();
  m_parser->update_attributes();
  m_perf.add("ad_parse_input_update_attributes_ms", timer.elapsed_ms());

  verboseStream << "driver rank " << m_params.rank << " fetching func data" << std::endl;
  timer.start();
  m_parser->fetchFuncData();
  m_perf.add("ad_parse_input_fetch_func_data_ms", timer.elapsed_ms());

  verboseStream << "driver rank " << m_params.rank << " fetching comm data" << std::endl;
  timer.start();
  m_parser->fetchCommData();
  m_perf.add("ad_parse_input_fetch_comm_data_ms", timer.elapsed_ms());

  verboseStream << "driver rank " << m_params.rank << " fetching counter data" << std::endl;
  timer.start();
  m_parser->fetchCounterData();
  m_perf.add("ad_parse_input_fetch_counter_data_ms", timer.elapsed_ms());


  verboseStream << "driver rank " << m_params.rank << " finished gathering data" << std::endl;


  // early SST buffer release
  m_parser->endStep();

  // count total number of events
  n_func_events = (unsigned long long)m_parser->getNumFuncData();
  n_comm_events = (unsigned long long)m_parser->getNumCommData();
  n_counter_events = (unsigned long long)m_parser->getNumCounterData();

  //Parse the new metadata for any attributes we want to maintain
  m_metadata_parser->addData(m_parser->getNewMetaData());

  verboseStream << "driver completed input parse for step " << step << std::endl;
  m_perf.add("ad_parse_input_total_ms", total_timer.elapsed_ms());

  return true;
}







//Extract parsed events and insert into the event manager
void Chimbuko::extractEvents(unsigned long &first_event_ts,
			     unsigned long &last_event_ts,
			     int step){
  PerfTimer timer;
  std::vector<Event_t> events = m_parser->getEvents();
  m_perf.add("ad_extract_events_get_events_ms", timer.elapsed_ms());
  timer.start();
  for(auto &e : events){
    if(e.type() != EventDataType::COUNT || !m_exec_ignore_counters.count(e.counter_id()) ){
      m_event->addEvent(e);
    }
  }
  m_perf.add("ad_extract_events_register_ms", timer.elapsed_ms());
  m_perf.add("ad_extract_events_event_count", events.size());
  if(events.size()){
    first_event_ts = events.front().ts();
    last_event_ts = events.back().ts();
  }else{
    first_event_ts = last_event_ts = -1; //no events!
  }
}

void Chimbuko::extractCounters(int rank, int step){
  if(!m_counter) throw std::runtime_error("Counter is not initialized");
  PerfTimer timer;
  for(size_t c=0;c<m_parser->getNumCounterData();c++){
    Event_t ev(m_parser->getCounterData(c),
	       EventDataType::COUNT,
	       c,
	       eventID(rank, step, c));
    try{
      m_counter->addCounter(ev);
    }catch(const std::exception &e){
      recoverable_error(std::string("extractCounters failed to register counter event :\"") + ev.get_json().dump() + "\"");
    }
  }
  m_perf.add("ad_extract_counters_get_register_ms", timer.elapsed_ms());
}


void Chimbuko::extractNodeState(){
  if(!m_monitoring) throw std::runtime_error("Monitoring is not initialized");
  if(!m_counter) throw std::runtime_error("Counter is not initialized");
  PerfTimer timer;
  m_monitoring->extractCounters(m_counter->getCountersByIndex());

  //Get the counters that monitoring is watching and tell the event manager to ignore them so they don't get attached to any function execution
  std::vector<int> cidx_mon_ignore = m_monitoring->getMonitoringCounterIndices();
  for(int i: cidx_mon_ignore){
    m_exec_ignore_counters.insert(i);
  }
  
  m_perf.add("ad_extract_node_state_ms", timer.elapsed_ms());
}




void Chimbuko::extractAndSendProvenance(const Anomalies &anomalies,
					const int step,
					const unsigned long first_event_ts,
					const unsigned long last_event_ts) const{
  //Optionally skip provenance data recording on certain steps
  if(m_params.prov_record_startstep != -1 && step < m_params.prov_record_startstep) return;
  if(m_params.prov_record_stopstep != -1 && step > m_params.prov_record_stopstep) return;
 
  //Gather provenance data on anomalies and send to provenance database
  if(m_params.prov_outputpath.length() > 0
#ifdef ENABLE_PROVDB
     || m_provdb_client->isConnected()
#endif
     ){
    //Get the provenance data
    PerfTimer timer;
    std::vector<nlohmann::json> anomaly_prov, normalevent_prov;
    m_anomaly_provenance->getProvenanceEntries(anomaly_prov, normalevent_prov, anomalies, step, first_event_ts, last_event_ts);
    m_perf.add("ad_extract_send_prov_provenance_data_generation_total_ms", timer.elapsed_ms());


    //Write and send provenance data
    if(anomaly_prov.size() > 0){
      timer.start();
      m_io->writeJSON(anomaly_prov, step, "anomalies");
      m_perf.add("ad_extract_send_prov_anom_data_io_write_ms", timer.elapsed_ms());

#ifdef ENABLE_PROVDB
      timer.start();
      m_provdb_client->sendMultipleDataAsync(anomaly_prov, ProvenanceDataType::AnomalyData); //non-blocking send
      m_perf.add("ad_extract_send_prov_anom_data_send_async_ms", timer.elapsed_ms());
#endif
    }

    if(normalevent_prov.size() > 0){
      timer.start();
      m_io->writeJSON(normalevent_prov, step, "normalexecs");
      m_perf.add("ad_extract_send_prov_normalexec_data_io_write_ms", timer.elapsed_ms());

#ifdef ENABLE_PROVDB
      timer.start();
      m_provdb_client->sendMultipleDataAsync(normalevent_prov, ProvenanceDataType::NormalExecData); //non-blocking send
      m_perf.add("ad_extract_send_prov_normalexec_data_send_async_ms", timer.elapsed_ms());
#endif
    }

  }//isConnected
}

void Chimbuko::sendNewMetadataToProvDB(int step) const{
  if(m_params.prov_outputpath.length() > 0
#ifdef ENABLE_PROVDB
     || m_provdb_client->isConnected()
#endif
     ){
    PerfTimer timer;
    std::vector<MetaData_t> const & new_metadata = m_parser->getNewMetaData();
    if(new_metadata.size() > 0){
      std::vector<nlohmann::json> new_metadata_j(new_metadata.size());
      for(size_t i=0;i<new_metadata.size();i++)
	new_metadata_j[i] = new_metadata[i].get_json();
      m_perf.add("ad_send_new_metadata_to_provdb_metadata_gather_ms", timer.elapsed_ms());      

      timer.start();
      m_io->writeJSON(new_metadata_j, step, "metadata");
      m_perf.add("ad_send_new_metadata_to_provdb_io_write_ms", timer.elapsed_ms());

#ifdef ENABLE_PROVDB
      timer.start();
      m_provdb_client->sendMultipleDataAsync(new_metadata_j, ProvenanceDataType::Metadata); //non-blocking send
      m_perf.add("ad_send_new_metadata_to_provdb_metadata_count", new_metadata.size());
      m_perf.add("ad_send_new_metadata_to_provdb_send_async_ms", timer.elapsed_ms());
#endif
    }

  }
}

void Chimbuko::gatherAndSendPSdata(const Anomalies &anomalies,
				   const int step,
				   const unsigned long first_event_ts,
				   const unsigned long last_event_ts) const{
  if(m_net_client && m_net_client->use_ps()){
    PerfTimer timer;

    //Gather function profile and anomaly statistics
    timer.start();
    ADLocalFuncStatistics prof_stats(m_params.program_idx, m_params.rank, step, &m_perf);
    prof_stats.gatherStatistics(m_event->getExecDataMap());
    prof_stats.gatherAnomalies(anomalies);
    m_perf.add("ad_gather_send_ps_data_gather_profile_stats_time_ms", timer.elapsed_ms());

    //Gather counter statistics
    timer.start();
    ADLocalCounterStatistics count_stats(m_params.program_idx, step, nullptr, &m_perf); //currently collect all counters
    count_stats.gatherStatistics(m_counter->getCountersByIndex());
    m_perf.add("ad_gather_send_ps_data_gather_counter_stats_time_ms", timer.elapsed_ms());

    //Gather anomaly metrics
    timer.start();
    ADLocalAnomalyMetrics metrics(m_params.program_idx, m_params.rank, step, first_event_ts, last_event_ts, anomalies);
    m_perf.add("ad_gather_send_ps_data_gather_metrics_time_ms", timer.elapsed_ms());

    //Send the data in a single communication
    timer.start();
    ADcombinedPSdata comb_stats(prof_stats, count_stats, metrics, &m_perf);
    comb_stats.send(*m_net_client);
    m_perf.add("ad_gather_send_ps_data_gather_send_all_stats_to_ps_time_ms", timer.elapsed_ms());
  }
}

bool Chimbuko::runFrame(unsigned long long& n_func_events,
			unsigned long long& n_comm_events,
			unsigned long long& n_counter_events,
			unsigned long& n_outliers){
  if(!m_is_initialized) throw std::runtime_error("Chimbuko is not initialized");

  int step = m_parser->getCurrentStep(); //gives -1 as initial value. step+1 is the expected value of step in parseInputStep and is used as a (non-fatal) check
  PerfTimer step_timer, timer;
  unsigned long io_step_first_event_ts, io_step_last_event_ts; //earliest and latest timestamps in io frame
  unsigned long long n_func_events_step, n_comm_events_step, n_counter_events_step; //event count in present step

  if(!parseInputStep(step, n_func_events_step, n_comm_events_step, n_counter_events_step)) return false;
  m_n_steps_accum_prd++;

  //Increment total events
  n_func_events += n_func_events_step;
  n_comm_events += n_comm_events_step;
  n_counter_events += n_counter_events_step;
  
  m_n_func_events_accum_prd += n_func_events_step;
  m_n_comm_events_accum_prd += n_comm_events_step;
  m_n_counter_events_accum_prd += n_counter_events_step;

  //Decide whether to report step progress
  bool do_step_report = enableVerboseLogging() || (m_params.step_report_freq > 0 && step % m_params.step_report_freq == 0);
  
  if(do_step_report){ headProgressStream(m_params.rank) << "driver rank " << m_params.rank << " starting step " << step
							<< ". Event count: func=" << n_func_events_step << " comm=" << n_comm_events_step
							<< " counter=" << n_counter_events_step << std::endl; }
  step_timer.start();
  
  //Extract counters and put into counter manager
  timer.start();
  extractCounters(m_params.rank, step);
  m_perf.add("ad_run_extract_counters_time_ms", timer.elapsed_ms());

  //Extract the node state
  extractNodeState();
  
  //Extract parsed events into event manager
  timer.start();
  extractEvents(io_step_first_event_ts, io_step_last_event_ts, step);
  m_perf.add("ad_run_extract_events_time_ms", timer.elapsed_ms());
    
  //Update the timestamp bounds for the analysis
  m_execdata_last_event_ts = io_step_last_event_ts;
  if(!m_execdata_first_event_ts_set){
    m_execdata_first_event_ts = io_step_first_event_ts;
    m_execdata_first_event_ts_set = true;
  }

  //Are we running the analysis this step?
  bool do_run_analysis = step % m_params.analysis_step_freq == 0;
  ADEvent::purgeReport purge_report;

  if(do_run_analysis){
    //Run the outlier detection algorithm on the events
    timer.start();
    Anomalies anomalies = m_outlier->run(step);
    m_perf.add("ad_run_anom_detection_time_ms", timer.elapsed_ms());
    m_perf.add("ad_run_anomaly_count", anomalies.nEventsRecorded(Anomalies::EventType::Outlier));
    m_perf.add("ad_run_n_exec_analyzed", anomalies.nEvents());

    int nout = anomalies.nEventsRecorded(Anomalies::EventType::Outlier);
    int nnormal = anomalies.nEvents() - nout; //this is the total number of normal events, not just of those that were recorded
    n_outliers += nout;
    m_n_outliers_accum_prd += nout;

    //Generate anomaly provenance for detected anomalies and send to DB
    timer.start();
    extractAndSendProvenance(anomalies, step, m_execdata_first_event_ts, m_execdata_last_event_ts);
    m_perf.add("ad_run_extract_send_provenance_time_ms", timer.elapsed_ms());
      
    //Send any new metadata to the DB
    timer.start();
    sendNewMetadataToProvDB(step);
    m_perf.add("ad_run_send_new_metadata_to_provdb_time_ms", timer.elapsed_ms());

    //Gather and send statistics and data to the pserver
    timer.start();
    gatherAndSendPSdata(anomalies, step, m_execdata_first_event_ts, m_execdata_last_event_ts);
    m_perf.add("ad_run_gather_send_ps_data_time_ms", timer.elapsed_ms());

    //Trim the call list
    timer.start();
    m_event->purgeCallList(m_params.anom_win_size, &purge_report); //we keep the last $anom_win_size events for each thread so that we can extend the anomaly window capture into the previous io step
    m_perf.add("ad_run_purge_calllist_ms", timer.elapsed_ms());

    //Flush counters
    timer.start();
    delete m_counter->flushCounters();
    m_perf.add("ad_run_flush_counters_ms", timer.elapsed_ms());

    //Enable update of analysis time window bound on next step
    m_execdata_first_event_ts_set = false;

    if(do_step_report){ headProgressStream(m_params.rank) << "driver rank " << m_params.rank << " function execution analysis complete: total=" << nout + nnormal << " normal=" << nnormal << " anomalous=" << nout << std::endl; }
  }//if(do_run_analysis)

  m_perf.add("ad_run_total_step_time_excl_parse_ms", step_timer.elapsed_ms());

  //Record periodic performance data
  if(m_params.perf_step > 0 && step % m_params.perf_step == 0){
    //Record the number of outstanding requests as a function of time, which can be used to gauge whether the throughput of the provDB is sufficient
#ifdef ENABLE_PROVDB
    m_perf_prd.add("provdb_incomplete_async_sends", m_provdb_client->getNoutstandingAsyncReqs());
#endif
    //Get the "data" memory usage (stack + heap)
    size_t total, resident;
    getMemUsage(total, resident);
    m_perf_prd.add("ad_mem_usage_kB", resident);

    m_perf_prd.add("io_steps", m_n_steps_accum_prd);

    //Write out how many events remain in the ExecData and how many unmatched correlation IDs there are
    m_perf_prd.add("call_list_purged", purge_report.n_purged);
    m_perf_prd.add("call_list_kept_protected", purge_report.n_kept_protected);
    m_perf_prd.add("call_list_kept_incomplete", purge_report.n_kept_incomplete);
    m_perf_prd.add("call_list_kept_window", purge_report.n_kept_window);
    
    //m_perf_prd.add("call_list_carryover_size", m_event->getCallListSize());
    m_perf_prd.add("n_unmatched_correlation_id", m_event->getUnmatchCorrelationIDevents().size());

    //Write accumulated outlier count
    m_perf_prd.add("outlier_count", m_n_outliers_accum_prd);

    //Write accumulated event counts
    m_perf_prd.add("event_count_func", m_n_func_events_accum_prd);
    m_perf_prd.add("event_count_comm", m_n_comm_events_accum_prd);
    m_perf_prd.add("event_count_counter", m_n_counter_events_accum_prd);

    //Reset the counts
    m_n_func_events_accum_prd = 0;
    m_n_comm_events_accum_prd = 0;
    m_n_counter_events_accum_prd = 0;
    m_n_outliers_accum_prd = 0;
    m_n_steps_accum_prd = 0;

    //These only write if both filename and output path is set
    m_perf_prd.write();
    m_perf.write(); //periodically write out aggregated perf stats also
  }

  if(do_step_report){ headProgressStream(m_params.rank) << "driver rank " << m_params.rank << " completed step " << step << std::endl; }
  return true;
}

void Chimbuko::run(unsigned long long& n_func_events,
		   unsigned long long& n_comm_events,
		   unsigned long long& n_counter_events,
		   unsigned long& n_outliers,
		   unsigned long& frames){
  if( m_params.max_frames == 0 ) return;

  while(runFrame(n_func_events,n_comm_events,n_counter_events,n_outliers)){
    ++frames;
    
    if (m_params.only_one_frame)
      break;

    if( m_params.max_frames > 0 && frames == m_params.max_frames )
      break;
    
    if (m_params.interval_msec)
      std::this_thread::sleep_for(std::chrono::milliseconds(m_params.interval_msec));
  }
}
