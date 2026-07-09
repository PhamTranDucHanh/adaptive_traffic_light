#ifndef PUBLISHER_COLLECTOR_H
#define PUBLISHER_COLLECTOR_H

#include <common/config.h>

class PublisherCollector {
 public:
  void receiveSignalOutput(const SignalOutput& output);

  DataCollect packageData();

  void publishToAnalytics(const DataCollect& data);

  HealthStatus sendHeartbeat() const;

 private:
  bool detectCycleComplete(PhaseId phase);

  bool detectEmergencyTransition(bool emergency);

 private:
  SignalOutput sampleBuffer[MAX_SAMPLES];

  uint16_t sampleCount;

  PhaseId cycleStartPhase;

  bool lastEmergencyState;

  bool cycleComplete;
};

#endif