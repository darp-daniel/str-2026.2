#include "alarm.hpp"

Alarm::Alarm() { active = false; }

Alarm::~Alarm() {}

Alarm::ativar() {
  active = true;
  timeActivated = std::chrono::steady_clock::now();
}

Alarm::desativar() { active = false; }

Alarm::chamarBlock {
  if (active) {
    if (dt_ms - timeActivated <= 5000) {
      blockActuator.lockByOverride();
      return true;
    }
  }
  return false;
}

Alarm::isActive() { return active; }
