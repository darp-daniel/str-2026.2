#pragma once
#include <chrono>

class Ignition {
private:
  std::chrono::steady_clock::time_point timeIgnition;
  float frequency;
  bool state;

public:
  void lerSwitch(bool switch);
  void combustao(std::chrono::steady_clock::time_point dt_ms,
                 bool isVehicleBlocked);

  bool getState();
  float getFrequency();
}
