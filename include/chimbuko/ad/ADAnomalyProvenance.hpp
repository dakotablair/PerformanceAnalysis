#pragma once

#include <chimbuko/ad/ADEvent.hpp>
#include <chimbuko/param.hpp>
#include "chimbuko/ad/ADCounter.hpp"
#include "chimbuko/ad/ADMetadataParser.hpp"

namespace chimbuko{

  /**
   * @brief A class that gathers provenance data associated with a detected anomaly
   */
  class ADAnomalyProvenance{
  public:
    /**
     * @brief Extract the provenance information and store in internal JSON fields
     * @param call The anomalous execution
     * @param event_man The event manager
     * @param func_stats The function statistics
     * @param counters The counter manager
     * @param metadata The metadata manager
     * @param window_size The number of events (on this process/rank/thread) either side of the anomalous event that are captured in the window field
     * @param io_step Index of io step
     * @param io_step_tstart Timestamp of beginning of io frame
     * @param io_step_tend Timestamp of end of io frame
     */     
    ADAnomalyProvenance(const ExecData_t &call,
			const ADEvent &event_man, //for stack trace
			const ParamInterface &func_stats, //for func stats
			const ADCounter &counters, //for counters
			const ADMetadataParser &metadata, //for env information including GPU context/device/stream
			const int window_size,
			const int io_step, 
			const unsigned long io_step_tstart, const unsigned long io_step_tend);

    /**
     * @brief Serialize anomaly data into JSON construct
     */
    nlohmann::json get_json() const;
    
  private:
    /**
     * @brief Get the call stack
     */
    void getStackInformation(const ExecData_t &call, const ADEvent &event_man);
    
    /**
     * @brief Get counters in execution window
     */
    void getWindowCounters(const ExecData_t &call);

    /**
     * @brief Determine if it is a GPU event, and if so get the context
     */
    void getGPUeventInfo(const ExecData_t &call, const ADEvent &event_man, const ADMetadataParser &metadata);

    /**
     * @brief Get the execution window
     * @param window_size The number of events either side of the call that are captured
     */
    void getExecutionWindow(const ExecData_t &call,
			    const ADEvent &event_man,
			    const int window_size);


    ExecData_t m_call; /**< The anomalous event*/
    std::vector<nlohmann::json> m_callstack; /**< Call stack from function back to root. Each entry is the function index and name */
    nlohmann::json m_func_stats; /**< JSON object containing run statistics of the anomalous function */
    std::vector<nlohmann::json> m_counters; /**< A list of counter events that occurred during the execution of the anomalous function*/
    bool m_is_gpu_event; /**< Is this an anomaly that occurred on a GPU? */
    nlohmann::json m_gpu_location; /**< If it was a GPU event, which device/context/stream did it occur on */
    nlohmann::json m_gpu_event_parent_info; /**< If a GPU event, info related to CPU event spawned it (name, thread, callstack) */
    nlohmann::json m_exec_window; /**< Window of function executions and MPI commuinications around anomalous execution*/
    int m_io_step; /**< IO step*/
    unsigned long m_io_step_tstart; /**< Timestamp of start of io step*/
    unsigned long m_io_step_tend; /**< Timestamp of end of io step*/
  };


};