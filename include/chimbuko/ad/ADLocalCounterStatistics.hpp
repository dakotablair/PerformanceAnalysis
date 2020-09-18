#pragma once

#include<unordered_set>
#include <chimbuko/ad/ADNetClient.hpp>
#include <chimbuko/ad/ADEvent.hpp>
#include <chimbuko/ad/ADCounter.hpp>
#include "chimbuko/util/PerfStats.hpp"

namespace chimbuko{

  /**
   * @brief A class that gathers local counter statistics and communicates them to the parameter server
   * @param step The current io step
   * @param which_counters The set of counters we are interested in (not all might appear in any given run). If nullptr all counters are accepted.
   * @param perf A pointer to a PerfStats instance for performance data monitoring
   */
  class ADLocalCounterStatistics{
  public:
    /**
     * @brief Data structure containing the data that is sent (in serialized form) to the parameter server
     */
    struct State{
      struct CounterData{
	std::string name; /**< Counter name*/
	RunStats::State stats; /**< Counter value statistics */
	
	/**
	 * @brief Serialize using cereal
	 */
	template<class Archive>
	void serialize(Archive & archive){
	  archive(name,stats);
	}
      };
      std::vector<CounterData> counters; /**< Statistics for all counters */
      
      /**
       * @brief Serialize using cereal
       */
      template<class Archive>
      void serialize(Archive & archive){
	archive(counters);
      }

      /**
       * Serialize into Cereal portable binary format
       */
      std::string serialize_cerealpb() const;
      
      /**
       * Serialize from Cereal portable binary format
       */     
      void deserialize_cerealpb(const std::string &strstate);
    };     


    ADLocalCounterStatistics(const int step,
			     const std::unordered_set<std::string> *which_counters, PerfStats *perf = nullptr):
      m_step(step), m_which_counter(which_counters), m_perf(perf)
    {}
				
    /**
     * @brief Add counters to internal statistics
     */
    void gatherStatistics(const CountersByIndex_t &cntrs_by_idx);


    /**
     * @brief update (send) counter statistics gathered during this io step to the connected parameter server
     * @param net_client The network client object
     * @return std::pair<size_t, size_t> [sent, recv] message size
     *
     * The message string is the output of get_json_state() in string format
     */
    std::pair<size_t, size_t> updateGlobalStatistics(ADNetClient &net_client) const;

    /**
     * @brief Attach a PerfStats object into which performance metrics are accumulated
     */
    void linkPerf(PerfStats* perf){ m_perf = perf; }

    /**
     * @brief Get the map of counter name to statistics
     */
    const std::unordered_map<std::string , RunStats> &getStats() const{ return m_stats; }
    
    /**
     * @brief Get the JSON object that is sent to the parameter server
     *
     * The string form of this object is sent to the pserver using updateGlobalStatistics
     */
    nlohmann::json get_json_state() const;

    /**
     * @brief Get the State object that is sent to the parameter server
     *
     * The string form of this object is sent to the pserver using updateGlobalStatistics
     */
    State get_state() const;  

    /**
     * @brief Set the statistics for a particular counter (must be in the list of counters being collected). Primarily used for testing.
     */
    void setStats(const std::string &counter, const RunStats &to);
    
  protected:
    /**
     * @brief update (send) counter statistics gathered during this io step to the connected parameter server
     * 
     * @param net_client The network client object
     * @param l_stats local statistics
     * @param step step (or frame) number
     * @return std::pair<size_t, size_t> [sent, recv] message size
     */
    static std::pair<size_t, size_t> updateGlobalStatistics(ADNetClient &net_client, const std::string &l_stats, int step);

    int m_step; /**< io step */
    const std::unordered_set<std::string> *m_which_counter; /** The set of counters whose statistics we are accumulating */
    std::unordered_map<std::string , RunStats> m_stats; /**< map of counter to statistics */

    PerfStats *m_perf; /**< Store performance data */
  };
  
  




};
