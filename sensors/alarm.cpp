#include "alarm.hpp"
Alarm::Alarm() : active(false) {}
Alarm::~Alarm() = default;
void Alarm::ativar() { active = true; timeActivated = std::chrono::steady_clock::now(); }
void Alarm::desativar() { active = false; }
bool Alarm::chamarBlock(std::chrono::steady_clock::time_point agora, Block &blockActuator) {
  if (active && std::chrono::duration_cast<std::chrono::milliseconds>(agora-timeActivated).count() >= MAX_TIME_ACTIVE) {
    if (!blockActuator.isBlocked()) { blockActuator.lockByOverride(); return true; }
  }
  return false;
}
bool Alarm::isActive() { return active; }
