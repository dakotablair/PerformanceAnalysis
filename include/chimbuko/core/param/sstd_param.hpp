#pragma once
#include <chimbuko_config.h>
#include "chimbuko/core/param.hpp"
#include "chimbuko/core/util/RunStats.hpp"
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace chimbuko {

  /**
   * @@brief Implementation of ParamInterface for anomaly detection based on function time distribution (mean, std. dev., etc)
   */
  class SstdParam : public ParamInterface
  {
  public:
    SstdParam();
    ~SstdParam();

    /**
     * @brief Clear all statistics
     */
    void clear() override;

    /**
     * @brief Get the number of models for which statistics are being collected
    */
    size_t size() const override { return m_runstats.size(); }

    /**
     * @brief Convert internal run statistics to string format for IO
     * @return Run statistics in string format
     */
    std::string serialize() const override;

    /**
     * @brief Update the internal run statistics with those included in the serialized input map
     * @param parameters The parameters in serialized format
     * @param return_update Indicates that the function should return a serialized copy of the updated parameters
     * @return An empty string or a serialized copy of the updated parameters depending on return_update
     */
    std::string update(const std::string& parameters, bool return_update=false) override;

    /**
     * @brief Set the internal run statistics to match those included in the serialized input map. Overwrite performed only for those keys in input.
     * @param runstats The serialized input map
     */
    void assign(const std::string& parameters) override;

    void show(std::ostream& os) const override;

    /**
     * @brief Convert a run statistics mapping into a Cereal portable binary representration
     * @param The run stats mapping
     * @return Run statistics in string format
     */
    static std::string serialize_cerealpb(const std::unordered_map<unsigned long, RunStats>& runstats);

    /**
     * @brief Convert a run statistics Cereal portable binary representation string into a map
     * @param[in] parameters The parameter string
     * @param[out] runstats The map between global function index and statistics
     */
    static void deserialize_cerealpb(const std::string& parameters,
			    std::unordered_map<unsigned long, RunStats>& runstats);

    /**
     * @brief Update the internal run statistics with those included in the input map
     * @param[in] runstats The map between global function index and statistics
     */
    void update(const std::unordered_map<unsigned long, RunStats>& runstats);

    /**
     * @brief Update the internal statistics with those included in another SstdParam instance.
     * @param[in] other The other SstdParam instance
     *
     * The other instance is locked during the process
     */
    void update(const SstdParam& other);

    /**
     * @brief Update the internal run statistics with those from another instance
     *
     * The instance will be dynamically cast to the derived type internally, and will throw an error if the types do not match
     */
    void update(const ParamInterface &other) override{ update(dynamic_cast<SstdParam const&>(other)); }

    /**
     * @brief Update the internal run statistics with those included in the input map. Input map is then updated to reflect new state.
     * @param[in,out] runstats The map between global function index and statistics
     */
    void update_and_return(std::unordered_map<unsigned long, RunStats>& runstats);

    /**
     * @brief Update the internal statistics with those included in another SstdParam instance. Other SstdParam is then updated to reflect new state.
     * @param[in,out] other The other SstdParam instance
     */
    void update_and_return(SstdParam& other) { update_and_return(other.m_runstats); }

    /**
     * @brief Set the internal run statistics to match those included in the input map. Overwrite performed only for those keys in input.
     * @param runstats The input map between global function index and statistics
     */
    void assign(const std::unordered_map<unsigned long, RunStats>& runstats);

    /**
     * @brief Get an element of the internal map
     * @param id The model index
     */
    RunStats& operator [](unsigned long id) { return m_runstats[id]; }

    /**
     * @brief Get the internal map between model index and statistics
     */
    const std::unordered_map<unsigned long, RunStats> & get_runstats() const{ return m_runstats; }

    /**
     * @brief Get the algorithm parameters associated with a given model index
     */
    nlohmann::json get_algorithm_params(const unsigned long model_idx) const override;

    /**
     * @brief Get the algorithm parameters for all model indices. Returns a map of model index to JSON-formatted parameters. Parameter format is algorithm dependent
     */
    std::unordered_map<unsigned long, nlohmann::json> get_all_algorithm_params() const override;

    /**
     * @brief Serialize the set of algorithm parameters in JSON form for purpose of inter-run persistence, format may differ from the above
     */
    nlohmann::json serialize_json() const override;

    /**
     * @brief De-serialize the set of algorithm parameters from JSON form created by serialize_json
     */
    void deserialize_json(const nlohmann::json &from) override;

  protected:
    /**
     * @brief Get the internal map of model index to statistics
     */
    std::unordered_map<unsigned long, RunStats> & access_runstats(){ return m_runstats; }

  private:
    std::unordered_map<unsigned long, RunStats> m_runstats; /**< Map of model index to statistics*/
  };

} // end of chimbuko namespace