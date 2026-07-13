#ifndef OUTPUT_SIMULATOR_H
#define OUTPUT_SIMULATOR_H

#include <common/config.h>

class OutputSimulator {
 public:
  void receiveSignalDisplay(const SignalDisplay& display);

  void formatConsole();

  void sendToParticipants();
};

#endif