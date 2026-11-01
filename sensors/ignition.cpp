#include "ignition.hpp"
#include <chrono>

Ignition::Ignition(float Dfrequency) {
  std::chrono::steady_clock::time_point timeActivated;
  bool state = flase;
  frequency = Dfrequency;
}

Ignition::~Ignition() {}

Ignition::lerSwitch(bool switch) { state = switch; }

Ignition::combustao(bool isVehicleBlocked) {
  if (state && !isVehicleBlocked) {
    timeActivated = std::chrono::steady_clock::now();
    frequency = 30.0f
  } else {
    frequency = 0.0f;
    if (!state)
      time = 0;
  }
}

Ignition::getState() { return state; }

Ignition::getFrequency() { return frequency; }
