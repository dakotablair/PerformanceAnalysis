#pragma once
#include <chimbuko_config.h>
#include <chimbuko/modules/performance_analysis/pserver/GlobalAnomalyStats.hpp>
#include <chimbuko/modules/performance_analysis/pserver/GlobalCounterStats.hpp>
#include <chimbuko/modules/performance_analysis/pserver/GlobalAnomalyMetrics.hpp>

namespace chimbuko{
  namespace modules{
    namespace performance_analysis{
  
      /**
       * @brief Net payload for receiving the combined data payload from the AD
       */
      class NetPayloadRecvCombinedADdata: public NetPayloadBase{
	GlobalAnomalyStats * m_global_anom_stats;
	GlobalCounterStats * m_global_counter_stats;
	GlobalAnomalyMetrics * m_global_anom_metrics;
      public:
	NetPayloadRecvCombinedADdata(GlobalAnomalyStats * global_anom_stats, GlobalCounterStats * global_counter_stats,
				     GlobalAnomalyMetrics * global_anom_metrics): m_global_anom_stats(global_anom_stats),
										  m_global_counter_stats(global_counter_stats),
										  m_global_anom_metrics(global_anom_metrics){}
	int kind() const override{ return MessageKind::AD_PS_COMBINED_STATS; }
	MessageType type() const override{ return MessageType::REQ_ADD; }
	void action(Message &response, const Message &message) override;
      };

      /**
       * @brief Net payload for receiving the combined data payload over multiple steps from the AD
       */
      class NetPayloadRecvCombinedADdataArray: public NetPayloadBase{
	GlobalAnomalyStats * m_global_anom_stats;
	GlobalCounterStats * m_global_counter_stats;
	GlobalAnomalyMetrics * m_global_anom_metrics;
      public:
	NetPayloadRecvCombinedADdataArray(GlobalAnomalyStats * global_anom_stats, GlobalCounterStats * global_counter_stats,
					  GlobalAnomalyMetrics * global_anom_metrics): m_global_anom_stats(global_anom_stats),
										       m_global_counter_stats(global_counter_stats),
										       m_global_anom_metrics(global_anom_metrics){}
	int kind() const override{ return MessageKind::AD_PS_COMBINED_STATS; }
	MessageType type() const override{ return MessageType::REQ_ADD; }
	void action(Message &response, const Message &message) override;
      };





    };
  }
}