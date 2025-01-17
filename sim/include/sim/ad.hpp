#pragma once
#include <chimbuko_config.h>
#include<chimbuko/ad/ADProvenanceDBclient.hpp>
#include<chimbuko/ad/ADMetadataParser.hpp>
#include<chimbuko/ad/ADNormalEventProvenance.hpp>
#include<chimbuko/ad/ADAnomalyProvenance.hpp>
#include<chimbuko/ad/ADEvent.hpp>
#include<chimbuko/ad/ADCounter.hpp>
#include<chimbuko/ad/ADOutlier.hpp>
#include<chimbuko/ad/ADio.hpp>
#include<map>

namespace chimbuko_sim{
  using namespace chimbuko;

  enum class CommType { Send, Recv };

  struct provDBsetup{
    bool use_local; /**< Use a local server instance (default true)*/
    int remote_server_nshards; /**< Number of database shards on the remote servere (default 1, only applicable for use_local = false)*/
    std::string remote_server_addr_dir; /**< Directory where remote server address files are written (only applicable for use_local = false)*/
    int remote_server_instances; /**< Number of remote server instances  (only applicable for use_local = false)*/
    provDBsetup(): use_local(true), remote_server_nshards(1){}
  };

  //An object that represents a rank of the AD
  class ADsim{
    std::unordered_map<unsigned long, std::list<ExecData_t> > m_all_execs; /**< Map of thread to execs */
    std::unordered_map<eventID, CallListIterator_t> m_eventid_map; /**< Map from event index to iterator */
    std::map<unsigned long, std::list<CallListIterator_t> > m_step_execs; /**< Events in any given step */
    std::map<unsigned long, unsigned long> m_entry_step_count; /**< Count of events starting on a given step (used to assign event indices) */

    unsigned long m_largest_step; /**< Largest step index of an inserted function execution thus far encountered */
    unsigned long m_program_start;
    unsigned long m_step_freq; /**< Frequency of IO steps */

    int m_window_size;
    int m_pid;
    int m_rid;
    ADMetadataParser m_metadata;
#ifdef ENABLE_PROVDB
    std::unique_ptr<ADProvenanceDBclient> m_pdb_client;
#else
    std::unique_ptr<ADio> m_prov_io; /**< Write to disk if provDB not in use */
#endif
    ADThreadNetClient *m_net_client; /**< The local net client. Only activated if an AD algorithm is used */
    ADOutlier* m_outlier; /**< The local outlier algorithm instance, if used*/
    std::unique_ptr<ADAnomalyProvenance> m_anomaly_provenance; /**< The provenance extractor*/
  public:
    /** 
     * @brief Initialize the AD simulator
     * @param window_size The number of events around an anomaly that are recorded in the provDB
     * @param pid The program index
     * @param rid The rank index
     * @param program_start Timestamp of program start
     * @param step_freq Frequency at which IO steps are to occur
     */
    void init(int window_size, int pid, int rid, unsigned long program_start, unsigned long step_freq, const provDBsetup &pdb_setup = provDBsetup());

    /** 
     * @brief Instantiate the AD simulator
     * @param window_size The number of events around an anomaly that are recorded in the provDB
     * @param pid The program index
     * @param rid The rank index
     * @param program_start Timestamp of program start
     * @param step_freq Frequency at which IO steps are to occur
     */
    ADsim(int window_size, int pid, int rid, unsigned long program_start, unsigned long step_freq, const provDBsetup &pdb_setup = provDBsetup()): ADsim(){
      init(window_size, pid, rid, program_start, step_freq, pdb_setup);
    }
    ADsim(): m_outlier(nullptr), m_net_client(nullptr){}

    ADsim(ADsim &&r);
    
    ~ADsim(){ 
      if(m_outlier) delete m_outlier; 
      if(m_net_client) delete m_net_client;
    }

#ifdef ENABLE_PROVDB
    ADProvenanceDBclient &getProvDBclient(){ return *m_pdb_client; }
#endif

    /**
     * @brief Add a function execution on a specific thread
     * @param thread The thread index
     * @param func_name The name of the function
     * @param start The timestamp of the function start
     * @param runtime The function duration
     * @param is_anomaly Tag the function as anomalous
     * @param outlier_score If anomalous, provide a score
     *
     * The function end time (start + runtime) must be within the current IO step's time window
     */
    CallListIterator_t addExec(const int thread,
			       const std::string &func_name,
			       unsigned long start,
			       unsigned long runtime,
			       bool is_anomaly = false,
			       double outlier_score = 0.);

    /**
     * @brief Modify the runtime of an existing execution
     * @param exec_it The iterator to the execution (output of previous addExec)
     * @param new_runtime The new function runtime
     */
    void updateRuntime(CallListIterator_t exec_it,
		       unsigned long new_runtime);


    /**
     * @brief Attach a counter to an execution t_delta us after the start of the execution
     * @param counter_name The counter name
     * @param value The counter value
     * @param t_delta The time after the start of the function execution at which the counter occurs
     * @param to The function execution
     */
    void attachCounter(const std::string &counter_name,
		       unsigned long value,
		       long t_delta,
		       CallListIterator_t to);

    /**
     * @brief Attach a communication event to an execution t_delta us after the start of the execution
     * @param type The type of communication
     * @param partner_rank The origin rank of a receive or the destination rank of a send
     * @param bytes The number of bytes communicated
     * @param t_delta The time after the start of the function execution at which the counter occurs
     * @param to The function execution
     */     
    void attachComm(CommType type,
		    unsigned long partner_rank,
		    unsigned long bytes,
		    long t_delta,
		    CallListIterator_t to);
   
    /**
     * @brief Register a thread index as corresponding to a GPU thread, allowing population of GPU information in provenance data
     */
    void registerGPUthread(const int tid);
    
    /**
     * @brief Register a GPU kernel event as originating from a cpu event
     * @param cpu_parent The parent execution
     * @param gpu_kern The child GPU kernel execution
     */
    void bindCPUparentGPUkernel(CallListIterator_t cpu_parent, CallListIterator_t gpu_kern);

    /**
     * @brief Bind a parent and child execution
     */
    static void bindParentChild(CallListIterator_t parent, CallListIterator_t child);

    /**
     * @brief Analyze the given IO step, sending the data to the pserver/provdb
     * 
     * When this is called the executions added since the start of the step will be gathered and analyzed, and the data send to the pserver/provdb
     */
    void step(const unsigned long step);

    /**
     * @brief Get the largest step index in the trace
     */
    unsigned long largest_step() const{ return m_largest_step; }

    /**
     * @brief Get the number of executions in a given step
     */
    size_t nStepExecs(unsigned long step) const;
  };

  /**
   * @brief Pretty print a function execution
   */
  inline std::ostream & operator<<(std::ostream &os, const CallListIterator_t it){
    os << it->get_json(true, true).dump(4);
    return os;
  }

};
