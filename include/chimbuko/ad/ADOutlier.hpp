#pragma once
#include <chimbuko_config.h>
#include<array>
#include<unordered_set>
#include "chimbuko/ad/ADEvent.hpp"
#include "chimbuko/ad/ExecData.hpp"
#include "chimbuko/util/RunStats.hpp"
#include "chimbuko/param.hpp"
#include "chimbuko/param/sstd_param.hpp"
#include "chimbuko/util/hash.hpp"
#include "chimbuko/ad/ADNetClient.hpp"
#include "chimbuko/util/PerfStats.hpp"
#include "chimbuko/util/Anomalies.hpp"

namespace chimbuko {

  /**
   * @brief abstract class for anomaly detection algorithms
   *
   */
  class ADOutlier {

  public:
    /**
     * @brief Enumeration of which statistic is used for outlier detection
     */
    enum OutlierStatistic { ExclusiveRuntime, InclusiveRuntime };

    /**
     * @brief Unified structure for passing the parameters of the AD algorithms to the factory method
     */
    struct AlgoParams{
      OutlierStatistic stat; /**< Which statistic is used */
    
      //SSTD
      double sstd_sigma; /**< The number of sigma that defines an outlier*/
    
      //HBOS/COPOD
      double hbos_thres; /**< The outlier threshold*/
      std::string func_threshold_file; /**< Name of a file containing per-function overrides of the threshold*/

      //HBOS
      bool glob_thres; /**< Should the global threshold be used? */
      int hbos_max_bins; /**< The maximum number of bins in a histogram */

      AlgoParams();
    };


    /**
     * @brief Construct a new ADOutlier object
     *
     */
    ADOutlier(OutlierStatistic stat = ExclusiveRuntime);
    /**
     * @brief Destroy the ADOutlier object
     *
     */
    virtual ~ADOutlier();

    /**
     * @brief Fatory method to select AD algorithm at runtime
     */
    static ADOutlier *set_algorithm(const std::string & algorithm, const AlgoParams &params);

    /**
     * @brief check if the parameter server is in use
     *
     * @return true if the parameter server is in use
     * @return false if the parameter server is not in use
     */
    bool use_ps() const { return m_use_ps; }

    /**
     * @brief copy a pointer to execution data map
     *
     * @param m
     * @see ADEvent
     */
    void linkExecDataMap(const ExecDataMap_t* m) { m_execDataMap = m; }

    /**
     * @brief Link the interface for communicating with the parameter server
     */
    void linkNetworkClient(ADNetClient *client);

    /**
     * @brief abstract method to run the implemented anomaly detection algorithm
     *
     * @param step step (or frame) number
     * @return data structure containing information on captured anomalies
     */
    virtual Anomalies run(int step=0) = 0;

    /**
     * @brief If linked, performance information on the sync_param routine will be gathered
     */
    void linkPerf(PerfStats* perf){ m_perf = perf; }

    /**
     * @brief Get the local copy of the global parameters
     * @return Pointer to a ParamInterface object
     */
    ParamInterface const* get_global_parameters() const{ return m_param; }

    /**
     * @brief Return true if the specified function is being ignored
     */
    bool ignoringFunction(const std::string &func) const;

    /**
     * @brief Set a function to be ignored by the outlier detection.
     *
     * All such events are flagged as normal
     */
    void setIgnoreFunction(const std::string &func);

  protected:
    /**
     * @brief abstract method to compute outliers (or anomalies)
     *
     * @param[out] outliers data structure containing captured anomalies
     * @param func_id function id
     * @param[in,out] data a list of function calls to inspect. Entries will be tagged as outliers
     * @return unsigned long the number of outliers (or anomalies)
     */
    virtual unsigned long compute_outliers(Anomalies &outliers,
					   const unsigned long func_id, std::vector<CallListIterator_t>& data) = 0;

    /**
     * @brief Synchronize the local model with the global model
     *
     * @param[in] param local model
     * @return std::pair<size_t, size_t> [sent, recv] message size
     *
     * If we are connected to the pserver, the local model will be merged remotely with the current global model on the server, and the new global model returned. The internal global model will be replaced by this.
     * If we are *not* connected to the pserver, the local model will be merged into the internal global model
     */
    virtual std::pair<size_t, size_t> sync_param(ParamInterface const* param);


    /**
     * @brief Set the statistic used for the anomaly detection
     */
    void setStatistic(OutlierStatistic to){ m_statistic = to; }

    /**
     * @brief Extract the appropriate statistic from an ExecData_t object
     */
    double getStatisticValue(const ExecData_t &e) const;
    
  protected:
    int m_rank;                              /**< this process rank                      */
    bool m_use_ps;                           /**< true if the parameter server is in use */
    ADNetClient* m_net_client;                 /**< interface for communicating to parameter server */

    std::unordered_map< std::array<unsigned long, 4>, size_t, ArrayHasher<unsigned long,4> > m_local_func_exec_count; /**< Map(program id, rank id, thread id, func id) -> number of times encountered on this node*/

    const ExecDataMap_t * m_execDataMap;     /**< execution data map */
    ParamInterface * m_param;                /**< global parameters (kept in sync with parameter server) */

    PerfStats *m_perf;
  private:
    OutlierStatistic m_statistic; /** Which statistic to use for outlier detection */
    std::unordered_set<std::string> m_func_ignore; /**< A list of functions that are ignored by the anomaly detection (all flagged as normal events)*/
  };

  /**
   * @brief statistic analysis based anomaly detection algorithm
   *
   */
  class ADOutlierSSTD : public ADOutlier {

  public:
    /**
     * @brief Construct a new ADOutlierSSTD object
     *
     */
    ADOutlierSSTD(OutlierStatistic stat = ExclusiveRuntime, double sigma = 6.0);
    /**
     * @brief Destroy the ADOutlierSSTD object
     *
     */
    ~ADOutlierSSTD();

    /**
     * @brief Set the sigma value
     *
     * @param sigma sigma value
     */
    void set_sigma(double sigma) { m_sigma = sigma; }

    /**
     * @brief run this anomaly detection algorithm
     *
     * @param step step (or frame) number
     * @return data structure containing captured anomalies
     */
    Anomalies run(int step=0) override;

  protected:
    /**
     * @brief compute outliers (or anomalies) of the list of function calls
     *
     * @param[out] outliers Array of function calls that were tagged as outliers
     * @param func_id function id
     * @param data[in,out] a list of function calls to inspect
     * @return unsigned long the number of outliers (or anomalies)
     */
    unsigned long compute_outliers(Anomalies &outliers,
				   const unsigned long func_id, std::vector<CallListIterator_t>& data) override;

    /**
     * @brief Compute the anomaly score (probability) for an event assuming a Gaussian distribution
     */
    double computeScore(CallListIterator_t ev, const SstdParam &stats) const;

  private:
    double m_sigma; /**< sigma */
  };

  /**
   * @brief HBOS anomaly detection algorithm
   *
   */
  class ADOutlierHBOS : public ADOutlier {
  public:

    /**
     * @brief Construct a new ADOutlierHBOS object
     * @param stat Which statistic to use
     * @param threshold The threshold defining an outlier
     * @param use_global_threshold The threshold is maintained as part of the global model
     * @param maxbins The maximum number of bins in the histograms
     *
     */
    ADOutlierHBOS(OutlierStatistic stat = ExclusiveRuntime, double threshold = 0.99, bool use_global_threshold = true, int maxbins = 200);

    /**
     * @brief Destroy the ADOutlierHBOS object
     *
     */
    ~ADOutlierHBOS();

    /**
     * @brief Set the outlier detection threshold
     */
    void set_threshold(double to){ m_threshold = to; }

    /**
     * @brief Set the alpha value
     *
     * @param regularizer alpha value
     */
    void set_alpha(double alpha) { m_alpha = alpha; }

    /**
     * @brief Set the maximum number of histogram bins
     */
    void set_maxbins(int to){ m_maxbins = to; }
    
    /**
     * @brief run HBOS anomaly detection algorithm
     *
     * @param step step (or frame) number
     * @return data structure containing captured anomalies
     */
    Anomalies run(int step=0) override;

    /**
     * @brief Override the default threshold for a particular function
     * @param func The function name
     * @param threshold The new threshold
     *
     * Note: during the merge on the pserver, the merged threshold will be the larger of the values from the two inputs, hence the threshold should ideally be uniformly set for all ranks     
     */
    void overrideFuncThreshold(const std::string &func, const double threshold){ m_func_threshold_override[func] = threshold; }

    /**
     * @brief Get the threshold used by the given function
     */
    double getFunctionThreshold(const std::string &fname) const;

  protected:
    /**
     * @brief compute outliers (or anomalies) of the list of function calls
     *
     * @param[out] outliers Array of function calls that were tagged as outliers
     * @param func_id function id
     * @param data[in,out] a list of function calls to inspect
     * @return unsigned long the number of outliers (or anomalies)
     */
    unsigned long compute_outliers(Anomalies &outliers,
				   const unsigned long func_id, std::vector<CallListIterator_t>& data) override;

    /**
     * @brief Scott's rule for bin_width estimation during histogram formation
     */
    double _scott_binWidth(std::vector<double>& vals);
    
  private:
    double m_alpha; /**< Used to prevent log2 overflow */
    double m_threshold; /**< Threshold used to filter anomalies in HBOS*/
    std::unordered_map<std::string, double> m_func_threshold_override; /**< Optionally override the threshold for specific functions*/

    bool m_use_global_threshold; /**< Flag to use global threshold*/

    OutlierStatistic m_statistic; /**< Which statistic to use for outlier detection */
    int m_maxbins; /**< Maximum number of bin in the histograms */
  };

  /**
   * @brief COPOD anomaly detection algorithm
   *
   * A description of the algorithm can be found in 
   * Li, Z., Zhao, Y., Botta, N., Ionescu, C. and Hu, X. COPOD: Copula-Based Outlier Detection. IEEE International Conference on Data Mining (ICDM), 2020.
   * https://www.andrew.cmu.edu/user/yuezhao2/papers/20-icdm-copod-preprint.pdf
   *
   * The implementation is based on the pyOD implementation https://pyod.readthedocs.io/en/latest/_modules/pyod/models/copod.html
   * for which the computation of the outlier score differs slightly from that in the paper
   */
  class ADOutlierCOPOD : public ADOutlier {
  public:

    /**
     * @brief Construct a new ADOutlierCOPOD object
     *
     */
    ADOutlierCOPOD(OutlierStatistic stat = ExclusiveRuntime, double threshold = 0.99, bool use_global_threshold = true);

    /**
     * @brief Destroy the ADOutlierCOPOD object
     *
     */
    ~ADOutlierCOPOD();

    /**
     * @brief Set the alpha value
     *
     * @param regularizer alpha value
     */
    void set_alpha(double alpha) { m_alpha = alpha; }

    /**
     * @brief run HBOS anomaly detection algorithm
     *
     * @param step step (or frame) number
     * @return data structure containing captured anomalies
     */
    Anomalies run(int step=0) override;

    /**
     * @brief Override the default threshold for a particular function
     * @param func The function name
     * @param threshold The new threshold
     *
     * Note: during the merge on the pserver, the merged threshold will be the larger of the values from the two inputs, hence the threshold should ideally be uniformly set for all ranks     
     */
    void overrideFuncThreshold(const std::string &func, const double threshold){ m_func_threshold_override[func] = threshold; }

    /**
     * @brief Get the threshold used by the given function
     */
    double getFunctionThreshold(const std::string &fname) const;


  protected:
    /**
     * @brief compute outliers (or anomalies) of the list of function calls
     *
     * @param[out] outliers Array of function calls that were tagged as outliers
     * @param func_id function id
     * @param data[in,out] a list of function calls to inspect
     * @return unsigned long the number of outliers (or anomalies)
     */
    unsigned long compute_outliers(Anomalies &outliers,
				   const unsigned long func_id, std::vector<CallListIterator_t>& data) override;

    /**
     * @brief Scott's rule for bin_width estimation during histogram formation
     */
    double _scott_binWidth(std::vector<double>& vals);

    /**
     * @brief Computes Empirical CDF of input vector of function runtimes
     */
    std::vector<double> empiricalCDF(const std::vector<double>& runtimes, const bool sorted=true);

  private:
    double m_alpha; /**< Used to prevent log2 overflow */
    double m_threshold; /**< Threshold used to filter anomalies in COPOD*/
    std::unordered_map<std::string, double> m_func_threshold_override; /**< Optionally override the threshold for specific functions*/
    
    bool m_use_global_threshold; /**< Flag to use global threshold*/
    //double m_threshold; /**< sync with global threshold */
    OutlierStatistic m_statistic; /**< Which statistic to use for outlier detection */
  };


} // end of AD namespace
